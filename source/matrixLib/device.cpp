#include "device.h"

#include "patch.h"
#include "patchNames.h"

#include "baseLib/filesystem.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace matrixLib
{
	namespace
	{
		// bump when the voice model changes: the snapshot holds calibration done against it
		constexpr uint8_t SnapshotVersion = 4;
		constexpr char SnapshotMagic[] = "M1KSRAM";
		constexpr char SnapshotFile[] = "matrix1000.nvram";

		constexpr float BootRate = 48000.0f;

		// the firmware keeps up with four times the MIDI rate, which keeps controller and
		// pitch bend streams from queueing up behind the 31.25 kbaud UART
		constexpr uint32_t UartCyclesPerByte = Hardware::MidiCyclesPerByte / 4;

		// continuous messages that only need their latest value per block
		uint32_t coalesceKey(const synthLib::SMidiEvent& _ev)
		{
			if(!_ev.sysex.empty() || _ev.a < 0x80)
				return 0;
			switch(_ev.a & 0xf0)
			{
			case 0xb0:
				// switches, bank select, data entry and mode messages carry state in every value
				if(_ev.b == 0 || _ev.b == 32 || _ev.b == 6 || _ev.b == 38 || (_ev.b >= 64 && _ev.b <= 69) || _ev.b >= 96)
					return 0;
				return 0x10000u | (_ev.a << 8) | _ev.b;
			case 0xd0:
			case 0xe0: return 0x10000u | (_ev.a << 8);
			default:   return 0;
			}
		}

		uint32_t firmwareSum(const std::vector<uint8_t>& _rom)
		{
			uint32_t s = 0;
			for(const auto b : _rom)
				s = s * 31 + b;
			return s;
		}

		// LFO rate per speed value: the firmware's increment table at $AA1C times the tick scale
		constexpr uint16_t LfoIncrements[64] =
		{
			44, 62, 74, 88, 102, 116, 132, 148, 168, 172, 192, 214, 242, 276, 312, 356,
			410, 462, 520, 586, 656, 720, 780, 830, 886, 936, 992, 1058, 1130, 1192, 1262, 1338,
			1424, 1490, 1560, 1638, 1724, 1820, 1928, 2048, 2184, 2340, 2520, 2730, 2978, 3276, 3450, 3640,
			3856, 4096, 4370, 4682, 5042, 5462, 5958, 6554, 7282, 8192, 9364, 10924, 13108, 16386, 21846, 32770
		};
		constexpr double LfoHzPerIncrement = 5.9466e-4;

		struct LfoDivision { const char* name; double beats; };
		constexpr LfoDivision LfoDivisions[] =
		{
			{"Off", 0.0},
			{"4/1", 16.0}, {"2/1", 8.0}, {"1/1", 4.0},
			{"1/2 Dotted", 3.0}, {"1/2", 2.0}, {"1/2 Triplet", 4.0 / 3.0},
			{"1/4 Dotted", 1.5}, {"1/4", 1.0}, {"1/4 Triplet", 2.0 / 3.0},
			{"1/8 Dotted", 0.75}, {"1/8", 0.5}, {"1/8 Triplet", 1.0 / 3.0},
			{"1/16 Dotted", 0.375}, {"1/16", 0.25}, {"1/16 Triplet", 1.0 / 6.0},
			{"1/32", 0.125},
		};
		constexpr uint32_t LfoDivisionCount = sizeof(LfoDivisions) / sizeof(LfoDivisions[0]);
		constexpr double LfoSyncTolerance = 0.06;
		constexpr uint8_t LfoSpeedParam[2] = {80, 90};

		// without a host playhead, MIDI clock or SysEx tempo (the standalone app)
		constexpr float DefaultBpm = 120.0f;

		// nearest firmware speed for a rate, and how far off it is (ratio - 1)
		int nearestLfoSpeed(const double _hz, double& _error)
		{
			int best = 0;
			double bestErr = 1e9;
			for(int i = 0; i < 64; ++i)
			{
				const double hz = LfoIncrements[i] * LfoHzPerIncrement;
				const double err = std::fabs(std::log(hz / _hz));
				if(err < bestErr)
				{
					bestErr = err;
					best = i;
				}
			}
			_error = std::exp(bestErr) - 1.0;
			return best;
		}

		bool fileExists(const std::string& _path)
		{
			std::ifstream f(_path, std::ios::binary);
			return f.is_open();
		}

		// another process (the app and its AUv3 share the folder) never sees a partial file
		bool writeFileAtomic(const std::string& _path, const std::vector<uint8_t>& _data)
		{
			static std::atomic<uint32_t> s_counter{0};
			const auto id = static_cast<uint64_t>(std::chrono::high_resolution_clock::now().time_since_epoch().count()) + s_counter.fetch_add(1);
			const auto tmp = _path + ".tmp" + std::to_string(id);
			if(!baseLib::filesystem::writeFile(tmp, _data))
				return false;
			if(std::rename(tmp.c_str(), _path.c_str()) == 0)
				return true;
			std::remove(tmp.c_str());
			return false;
		}

		std::string withSlash(const std::string& _path)
		{
			if(_path.empty() || _path.back() == '/' || _path.back() == '\\')
				return _path;
			return _path + '/';
		}
	}

	Device::Device(const synthLib::DeviceCreateParams& _params) : synthLib::Device(_params)
	{
		const auto& rom = _params.romData;
		if(rom.size() < 0x8000)
			return;

		if(!m_machine.setFirmware(std::vector<uint8_t>(rom.begin(), rom.begin() + 0x8000)))
			return;

		if(rom.size() >= 0x18000)
		{
			m_machine.setPatchRom(std::vector<uint8_t>(rom.begin() + 0x8000, rom.begin() + 0x18000));
			m_hasPatchRom = true;
		}

		m_homePath = withSlash(_params.homePath);
		m_valid = true;

		boot();

		if(m_hasPatchRom && !m_homePath.empty())
			createRomBanks();

		m_machine.getHardware().getAcia().setCyclesPerByte(UartCyclesPerByte);
	}

	Device::~Device() = default;

	void Device::getSupportedSamplerates(std::vector<float>& _dst) const
	{
		_dst.push_back(44100.0f);
		_dst.push_back(48000.0f);
		_dst.push_back(88200.0f);
		_dst.push_back(96000.0f);
	}

	bool Device::setSamplerate(const float _samplerate)
	{
		if(!synthLib::Device::setSamplerate(_samplerate))
			return false;
		m_machine.setSamplerate(_samplerate);
		return true;
	}

	void Device::runSeconds(const float _seconds)
	{
		std::vector<float> buf(512);
		auto remaining = static_cast<size_t>(_seconds * m_machine.getSamplerate());
		while(remaining)
		{
			const auto n = std::min(remaining, buf.size());
			m_machine.render(buf.data(), n);
			remaining -= n;
		}
	}

	bool Device::loadSnapshot()
	{
		if(m_homePath.empty())
			return false;

		std::vector<uint8_t> data;
		if(!baseLib::filesystem::readFile(data, m_homePath + SnapshotFile))
			return false;

		constexpr size_t header = sizeof(SnapshotMagic) + 1 + 4;
		auto& sram = m_machine.getHardware().getSram();
		if(data.size() != header + sram.size())
			return false;
		if(std::memcmp(data.data(), SnapshotMagic, sizeof(SnapshotMagic)) != 0 || data[sizeof(SnapshotMagic)] != SnapshotVersion)
			return false;

		uint32_t sum;
		std::memcpy(&sum, &data[sizeof(SnapshotMagic) + 1], 4);
		if(sum != firmwareSum(getDeviceCreateParams().romData))
			return false;

		std::memcpy(sram.data(), &data[header], sram.size());
		return true;
	}

	void Device::saveSnapshot() const
	{
		if(m_homePath.empty())
			return;

		baseLib::filesystem::createDirectory(m_homePath);

		const auto& sram = const_cast<Machine&>(m_machine).getHardware().getSram();
		std::vector<uint8_t> data(SnapshotMagic, SnapshotMagic + sizeof(SnapshotMagic));
		data.push_back(SnapshotVersion);
		const auto sum = firmwareSum(getDeviceCreateParams().romData);
		data.insert(data.end(), reinterpret_cast<const uint8_t*>(&sum), reinterpret_cast<const uint8_t*>(&sum) + 4);
		data.insert(data.end(), sram.begin(), sram.end());
		writeFileAtomic(m_homePath + SnapshotFile, data);
	}

	void Device::boot()
	{
		m_machine.setSamplerate(BootRate);

		const bool warm = loadSnapshot();
		m_machine.reset();

		if(warm)
		{
			runSeconds(1.0f);
			return;
		}

		// cold start: the firmware initialises its RAM and calibrates every voice, which
		// takes about half a minute of machine time
		constexpr float step = 0.25f;
		float t = 0.0f;
		bool sawCalibration = false;
		while(t < 60.0f)
		{
			runSeconds(step);
			t += step;
			if(m_machine.isCalibrating())
				sawCalibration = true;
			else if(sawCalibration || t > 5.0f)
				break;
		}
		runSeconds(1.0f);

		saveSnapshot();
	}

	std::string Device::romBankFileName(const uint32_t _bank)
	{
		return "!ROM Bank " + std::to_string(_bank) + ".syx";
	}

	std::vector<uint8_t> Device::requestPatch(const uint32_t _number)
	{
		auto& hw = m_machine.getHardware();
		const uint8_t request[] = {0xf0, patch::IdOberheim, patch::IdMatrix, patch::OpRequest, 0x01, static_cast<uint8_t>(_number), 0xf7};
		for(const auto b : request)
			hw.midiIn(b);

		std::vector<uint8_t> reply;
		std::vector<uint8_t> tx;
		for(int i = 0; i < 200 && reply.size() < patch::SysexSize; ++i)
		{
			runSeconds(0.005f);
			tx.clear();
			hw.midiOut(tx);
			for(const auto b : tx)
			{
				if(b == 0xf0)
					reply.clear();
				if(b == 0xf0 || !reply.empty())
					reply.push_back(b);
			}
		}
		return reply;
	}

	void Device::createRomBanks()
	{
		bool missing = false;
		for(uint32_t bank = 2; bank <= 9 && !missing; ++bank)
			missing = !fileExists(m_homePath + romBankFileName(bank));
		if(!missing)
			return;

		auto& hw = m_machine.getHardware();

		// the dumps only need to be correct, not real time
		hw.getAcia().setCyclesPerByte(UartCyclesPerByte);

		for(uint32_t bank = 2; bank <= 9; ++bank)
		{
			const auto file = m_homePath + romBankFileName(bank);
			if(fileExists(file))
				continue;

			const uint8_t setBank[] = {0xf0, patch::IdOberheim, patch::IdMatrix, patch::OpSetBank, static_cast<uint8_t>(bank), 0xf7};
			for(const auto b : setBank)
				hw.midiIn(b);
			runSeconds(0.05f);

			std::vector<uint8_t> out;
			for(uint32_t n = 0; n < 100; ++n)
			{
				const auto reply = requestPatch(n);
				auto data = patch::decode(reply);
				if(!data)
				{
					out.clear();
					break;
				}
				patch::setName(*data, g_factoryPatchNames[bank * 100 + n]);
				const auto msg = patch::encode(*data, patch::OpSinglePatch, static_cast<uint8_t>(n));
				out.insert(out.end(), msg.begin(), msg.end());
			}

			if(!out.empty())
				writeFileAtomic(file, out);
		}

		// return to the state the snapshot describes
		loadSnapshot();
		m_machine.reset();
		runSeconds(1.0f);
	}

	uint32_t Device::getLfoDivisionCount()
	{
		return LfoDivisionCount;
	}

	const char* Device::getLfoDivisionName(const uint32_t _division)
	{
		return _division < LfoDivisionCount ? LfoDivisions[_division].name : "";
	}

	void Device::setLfoSync(const uint32_t _lfo, const uint32_t _division)
	{
		m_lfoSync[_lfo & 1].store(_division < LfoDivisionCount ? _division : 0);
		m_lfoSyncDirty.store(true);
	}

	bool Device::hasTempoSource() const
	{
		return m_hostBpm.load() > 0.0f || m_clockBpm.load() > 0.0f || m_sysexBpm.load() > 0.0f;
	}

	float Device::getHostBpm() const
	{
		const float host = m_hostBpm.load();
		if(host > 0.0f)
			return host;
		const float clock = m_clockBpm.load();
		if(clock > 0.0f)
			return clock;
		const float sysex = m_sysexBpm.load();
		return sysex > 0.0f ? sysex : DefaultBpm;
	}

	bool Device::isLfoDivisionReachable(const uint32_t _division) const
	{
		const float bpm = getHostBpm();
		if(_division == 0 || _division >= LfoDivisionCount)
			return true;
		if(bpm <= 0.0f)
			return false;
		double err;
		nearestLfoSpeed(bpm / 60.0 / LfoDivisions[_division].beats, err);
		return err <= LfoSyncTolerance;
	}

	void Device::sendLfoSync(const bool _force)
	{
		const float bpm = getHostBpm();
		m_sentBpm = bpm;
		auto& hw = m_machine.getHardware();

		for(uint32_t lfo = 0; lfo < 2; ++lfo)
		{
			const auto division = m_lfoSync[lfo].load();
			if(division == 0 || bpm <= 0.0f)
			{
				// give the patch its own speed back
				if(m_sentSpeed[lfo] >= 0 && m_patchLfoSpeed[lfo] >= 0)
					for(const auto b : patch::createParamChange(LfoSpeedParam[lfo], m_patchLfoSpeed[lfo]))
						hw.midiIn(b);
				m_sentSpeed[lfo] = -1;
				continue;
			}
			double err;
			const int speed = nearestLfoSpeed(bpm / 60.0 / LfoDivisions[division].beats, err);
			if(!_force && speed == m_sentSpeed[lfo])
				continue;
			m_sentSpeed[lfo] = speed;
			for(const auto b : patch::createParamChange(LfoSpeedParam[lfo], speed))
				hw.midiIn(b);
		}
	}

	bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>&)
	{
		// tempo for the LFO sync, never forwarded to the firmware
		const auto& sx = _ev.sysex;
		if(sx.size() == 7 && sx[0] == 0xf0 && sx[1] == 0x7d && sx[2] == 0x54 && sx[6] == 0xf7)
		{
			const uint32_t us = (static_cast<uint32_t>(sx[3] & 0x7f) << 14) | ((sx[4] & 0x7f) << 7) | (sx[5] & 0x7f);
			if(us > 0)
				m_sysexBpm.store(static_cast<float>(60000000.0 / us));
			return true;
		}

		if(_ev.sysex.empty() && _ev.a == 0xf8)
		{
			// MIDI clock, 24 per quarter note: the tempo is taken over a whole beat
			const uint64_t t = m_sampleTime + _ev.offset;
			const auto rate = static_cast<double>(m_machine.getSamplerate());
			if(m_lastClock == 0 || t - m_lastClock > static_cast<uint64_t>(rate))
			{
				m_clockCount = 0;
				m_clockBeatStart = t;
			}
			else if(++m_clockCount == 24)
			{
				m_clockBpm.store(static_cast<float>(60.0 * rate / static_cast<double>(t - m_clockBeatStart)));
				m_clockCount = 0;
				m_clockBeatStart = t;
			}
			m_lastClock = t;
			return true;
		}

		m_pending.push_back({_ev.offset, _ev});
		return true;
	}

	void Device::pushMidi(const synthLib::SMidiEvent& _ev)
	{
		auto& hw = m_machine.getHardware();

		if(!_ev.sysex.empty())
		{
			for(const auto b : _ev.sysex)
				hw.midiIn(b);
			// a new patch or an edit of the speed itself replaces the synced value
			const auto& sx = _ev.sysex;
			if(sx.size() > 5 && sx[1] == patch::IdOberheim && sx[2] == patch::IdMatrix)
			{
				if(sx[3] == patch::OpEditBuffer)
				{
					if(const auto data = patch::decode(sx))
						for(uint32_t lfo = 0; lfo < 2; ++lfo)
							m_patchLfoSpeed[lfo] = (*data)[patch::findParam(LfoSpeedParam[lfo])->byte];
					m_lfoSyncDirty.store(true);
				}
				else if(sx[3] == patch::OpSinglePatch)
					m_lfoSyncDirty.store(true);
				else if(sx[3] == patch::OpParameter && (sx[4] == LfoSpeedParam[0] || sx[4] == LfoSpeedParam[1]))
				{
					m_patchLfoSpeed[sx[4] == LfoSpeedParam[0] ? 0 : 1] = sx[5] & 0x7f;
					m_lfoSyncDirty.store(true);
				}
			}
			return;
		}

		const uint8_t status = _ev.a;
		if(status < 0x80)
			return;

		// the firmware has no use for clock and active sensing, and every byte costs 320 us
		if(status >= 0xf8)
			return;

		if((status & 0xf0) == 0xc0)
		{
			m_patchLfoSpeed[0] = m_patchLfoSpeed[1] = -1;
			m_lfoSyncDirty.store(true);
		}

		hw.midiIn(status);
		switch(status & 0xf0)
		{
		case 0xc0:
		case 0xd0:
			hw.midiIn(_ev.b & 0x7f);
			break;
		case 0xf0:
			if(status == 0xf1 || status == 0xf3)
				hw.midiIn(_ev.b & 0x7f);
			else if(status == 0xf2)
			{
				hw.midiIn(_ev.b & 0x7f);
				hw.midiIn(_ev.c & 0x7f);
			}
			break;
		default:
			hw.midiIn(_ev.b & 0x7f);
			hw.midiIn(_ev.c & 0x7f);
			break;
		}
	}

	void Device::processAudio(const synthLib::TAudioInputs&, const synthLib::TAudioOutputs& _outputs, const size_t _samples)
	{
		if(m_mono.size() < _samples)
			m_mono.resize(_samples);

		std::stable_sort(m_pending.begin(), m_pending.end(), [](const PendingEvent& _a, const PendingEvent& _b) { return _a.offset < _b.offset; });

		// drop continuous messages that a later one of the same kind replaces in this block
		for(size_t i = 0; i < m_pending.size(); ++i)
		{
			const auto key = coalesceKey(m_pending[i].event);
			if(!key)
				continue;
			for(size_t j = i + 1; j < m_pending.size(); ++j)
			{
				if(coalesceKey(m_pending[j].event) == key)
				{
					m_pending[i].event.a = 0;
					break;
				}
			}
		}

		size_t pos = 0;
		size_t ev = 0;
		while(pos < _samples)
		{
			while(ev < m_pending.size() && m_pending[ev].offset <= pos)
				pushMidi(m_pending[ev++].event);

			size_t end = _samples;
			if(ev < m_pending.size())
				end = std::min<size_t>(end, std::max<size_t>(m_pending[ev].offset, pos + 1));

			m_machine.render(m_mono.data() + pos, end - pos);
			pos = end;
		}
		while(ev < m_pending.size())
			pushMidi(m_pending[ev++].event);
		m_pending.clear();

		m_sampleTime += _samples;
		if(m_lastClock && m_sampleTime - m_lastClock > static_cast<uint64_t>(m_machine.getSamplerate()))
			m_clockBpm.store(0.0f);

		const bool anySync = m_lfoSync[0].load() || m_lfoSync[1].load();
		if(m_lfoSyncDirty.exchange(false))
			sendLfoSync(true);
		else if(anySync && std::fabs(getHostBpm() - m_sentBpm) > 0.05f)
			sendLfoSync(false);

		for(size_t i = 0; i < _samples; ++i)
		{
			if(_outputs[0]) _outputs[0][i] = m_mono[i];
			if(_outputs[1]) _outputs[1][i] = m_mono[i];
		}
	}

	void Device::parseMidiOut(const uint8_t _byte)
	{
		if(_byte == 0xf0)
		{
			m_txSysex.assign(1, _byte);
			return;
		}
		if(!m_txSysex.empty())
		{
			m_txSysex.push_back(_byte);
			if(_byte == 0xf7)
			{
				synthLib::SMidiEvent e(synthLib::MidiEventSource::Device);
				e.sysex = std::move(m_txSysex);
				m_midiOut.push_back(std::move(e));
				m_txSysex.clear();
			}
			return;
		}
		if(_byte >= 0xf8)
			return;

		if(_byte & 0x80)
		{
			// F4-F7 carry no data
			if(_byte >= 0xf4)
			{
				if(_byte != 0xf7)
					m_midiOut.emplace_back(synthLib::MidiEventSource::Device, _byte, 0, 0);
				m_txRunningStatus = 0;
			}
			else
				m_txRunningStatus = _byte;
			m_txShort.clear();
			return;
		}
		if(!m_txRunningStatus)
			return;

		m_txShort.push_back(_byte);
		const auto type = m_txRunningStatus & 0xf0;
		const size_t len = (type == 0xc0 || type == 0xd0 || m_txRunningStatus == 0xf1 || m_txRunningStatus == 0xf3) ? 1 : 2;
		if(m_txShort.size() == len)
		{
			m_midiOut.emplace_back(synthLib::MidiEventSource::Device, m_txRunningStatus, m_txShort[0], len > 1 ? m_txShort[1] : 0);
			m_txShort.clear();
			if(type == 0xf0)
				m_txRunningStatus = 0;
		}
	}

	void Device::readMidiOut(std::vector<synthLib::SMidiEvent>& _midiOut)
	{
		m_txBytes.clear();
		m_machine.getHardware().midiOut(m_txBytes);
		for(const auto b : m_txBytes)
			parseMidiOut(b);

		_midiOut.insert(_midiOut.end(), m_midiOut.begin(), m_midiOut.end());
		m_midiOut.clear();
	}
}
