#include "romBankGenerator.h"

#include "matrixMachine.h"
#include "patch.h"
#include "patchNames.h"
#include "romloader.h"

#include "baseLib/filesystem.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <memory>
#include <thread>
#include <vector>

namespace matrixLib
{
	namespace
	{
		constexpr float BootRate = 48000.0f;

		// the dumps only need to be correct, not real time
		constexpr uint32_t UartCyclesPerByte = Hardware::MidiCyclesPerByte / 4;

		std::string withSlash(const std::string& _path)
		{
			if(_path.empty() || _path.back() == '/' || _path.back() == '\\')
				return _path;
			return _path + '/';
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

		class Session
		{
		public:
			explicit Session(const std::atomic<bool>* _cancel) : m_cancel(_cancel) {}

			bool cancelled() const { return m_cancel && m_cancel->load(); }

			bool boot(const RomSet& _roms)
			{
				if(!m_machine.setFirmware(_roms.firmware))
					return false;
				m_machine.setPatchRom(_roms.patchRom);
				m_machine.setSamplerate(BootRate);
				m_machine.reset();

				// cold start: the firmware initialises its RAM and calibrates every voice
				constexpr float step = 0.25f;
				float t = 0.0f;
				bool sawCalibration = false;
				while(t < 60.0f && !cancelled())
				{
					runSeconds(step);
					t += step;
					if(m_machine.isCalibrating())
						sawCalibration = true;
					else if(sawCalibration || t > 5.0f)
						break;
				}
				runSeconds(1.0f);

				// only a firmware patch load points the remote edit target at the edit buffer
				auto& hw = m_machine.getHardware();
				hw.midiIn(0xc0);
				hw.midiIn(0x00);
				runSeconds(0.25f);
				return !cancelled();
			}

			bool dumpBank(const uint32_t _bank, std::vector<uint8_t>& _out)
			{
				auto& hw = m_machine.getHardware();
				hw.getAcia().setCyclesPerByte(UartCyclesPerByte);

				const uint8_t setBank[] = {0xf0, patch::IdOberheim, patch::IdMatrix, patch::OpSetBank, static_cast<uint8_t>(_bank), 0xf7};
				for(const auto b : setBank)
					hw.midiIn(b);
				runSeconds(0.05f);

				_out.clear();
				for(uint32_t n = 0; n < 100; ++n)
				{
					auto data = patch::decode(requestPatch(n));
					if(!data || cancelled())
						return false;
					patch::setName(*data, g_factoryPatchNames[_bank * 100 + n]);
					const auto msg = patch::encode(*data, patch::OpSinglePatch, static_cast<uint8_t>(n));
					_out.insert(_out.end(), msg.begin(), msg.end());
				}
				return true;
			}

		private:
			void runSeconds(const float _seconds)
			{
				float buf[512];
				auto remaining = static_cast<size_t>(_seconds * m_machine.getSamplerate());
				// nobody hears this audio, so the voices are skipped outside calibration
				m_machine.setCpuOnly(true);
				while(remaining && !cancelled())
				{
					const auto n = std::min(remaining, std::size(buf));
					m_machine.render(buf, n);
					remaining -= n;
				}
				m_machine.setCpuOnly(false);
			}

			std::vector<uint8_t> requestPatch(const uint32_t _number)
			{
				auto& hw = m_machine.getHardware();
				const uint8_t request[] = {0xf0, patch::IdOberheim, patch::IdMatrix, patch::OpRequest, 0x01, static_cast<uint8_t>(_number), 0xf7};
				for(const auto b : request)
					hw.midiIn(b);

				std::vector<uint8_t> reply;
				std::vector<uint8_t> tx;
				for(int i = 0; i < 200 && reply.size() < patch::SysexSize && !cancelled(); ++i)
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

			const std::atomic<bool>* m_cancel;
			Machine m_machine;
		};

		std::atomic<bool> g_started{false};
		std::atomic<uint32_t> g_completed{0};

		// joined before the module goes away; cancel makes a running boot stop within a few ms
		struct Runner
		{
			std::thread thread;
			std::atomic<bool> cancel{false};
			std::atomic<bool> done{false};

			~Runner()
			{
				if(!thread.joinable())
					return;
				cancel = true;
#ifdef _WIN32
				// a join under the loader lock (DLL unload) would deadlock
				while(!done)
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
				thread.detach();
#else
				thread.join();
#endif
			}
		};

		Runner& runner()
		{
			static Runner r;
			return r;
		}
	}

	std::string RomBankGenerator::fileName(const uint32_t _bank)
	{
		return "!ROM Bank " + std::to_string(_bank) + ".syx";
	}

	bool RomBankGenerator::isMissing(const std::string& _folder)
	{
		const auto folder = withSlash(_folder);
		for(uint32_t bank = 2; bank <= 9; ++bank)
			if(!fileExists(folder + fileName(bank)))
				return true;
		return false;
	}

	bool RomBankGenerator::generate(const std::string& _folder, const RomSet& _roms, const std::atomic<bool>* _cancel)
	{
		if(!_roms.isValid() || _folder.empty())
			return false;
		const auto folder = withSlash(_folder);
		if(!isMissing(folder))
			return true;
		baseLib::filesystem::createDirectory(folder);

		// the machine is large: heap, and gone as soon as the banks are written
		auto session = std::make_unique<Session>(_cancel);
		if(!session->boot(_roms))
			return false;

		std::vector<uint8_t> out;
		bool ok = true;
		for(uint32_t bank = 2; bank <= 9 && ok; ++bank)
		{
			const auto file = folder + fileName(bank);
			if(fileExists(file))
				continue;
			ok = session->dumpBank(bank, out) && writeFileAtomic(file, out);
		}
		return ok;
	}

	void RomBankGenerator::startOnce(const std::string& _folder)
	{
		if(_folder.empty() || g_started.load() || !isMissing(_folder))
			return;
		auto roms = RomLoader::findROM();
		if(!roms.isValid() || g_started.exchange(true))
			return;

		auto& r = runner();
		r.thread = std::thread([&r, folder = _folder, roms = std::move(roms)]
		{
			if(generate(folder, roms, &r.cancel))
				g_completed.fetch_add(1);
			r.done = true;
		});
	}

	uint32_t RomBankGenerator::completedRuns()
	{
		return g_completed.load();
	}
}
