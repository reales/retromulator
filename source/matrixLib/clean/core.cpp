#include "core.h"
#include "cvDco.h"

#include "cvFilterAmp.h"
#include "tracking.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>

namespace matrixClean
{
	namespace
	{
		constexpr size_t si(Source _s) { return static_cast<size_t>(_s); }
		constexpr size_t di(Dest _d) { return static_cast<size_t>(_d); }

		int clampi(const int _v, const int _lo, const int _hi) { return _v < _lo ? _lo : (_v > _hi ? _hi : _v); }
		Mod scale(const Mod _v, const int _amount) { return static_cast<Mod>(static_cast<int64_t>(_v) * _amount / 63); }
		int32_t toUnits(const Mod _v, const int32_t _fullScale) { return static_cast<int32_t>(static_cast<int64_t>(_v) * _fullScale / ModFull); }

		// stub full scales in 1/256 semitone or 1/256 parameter step [R-MATRIX] [R-PITCH] [R-VCF]
	}

	Core::Core()
	{
		// sized for the largest bursts (bank dump reply, patch load) so the audio path does not allocate
		m_writes.reserve(16384);
		m_midiOut.reserve(32768);
		m_allocEvents.reserve(64);
		m_newAlloc.reserve(64);
		m_envExtPending.reserve(1024);
		m_msg.sysex.reserve(MidiIn::MaxSysex);
		reset();
	}

	void Core::reset()
	{
		m_master.reinit();
		const auto init = makeInitPatch();
		m_patches.fill(init);
		m_edit = init;
		m_current = 0;
		m_bank = 0;

		m_midi.reset();
		m_inQueue.clear();
		m_midiOut.clear();
		m_alloc.reset();
		m_alloc.setMode(effectiveMode());
		m_allocEvents.clear();

		for(uint32_t i = 0; i < VoiceCount; ++i)
		{
			auto& v = m_voices[i];
			v = VoiceState();
			for(uint32_t l = 0; l < 2; ++l)
				v.lfo[l].reset(Lfo::bootState(i, l));
			v.porta.reset(dco::IdleKey * 256);
			v.porta.setBootPitch(Portamento::bootPitch(i), Portamento::bootStartOffset(i), Portamento::bootStep(i), Portamento::bootDriftRef(i));
			for(auto& e : v.env)
				e.setBootArmed(true);
		}
		m_vibrato.reset();
		m_vibratoValue = 0;
		m_lfoExtPending = false;
		m_lfoBootProgram = true;
		latchVibrato();
		m_envExtPending.clear();
		m_envLoadUs = -1.0;

		m_bend = m_lever2 = m_lever3 = m_pedal1 = m_pedal2 = m_pressure = 0;
		m_bend8 = 128;
		m_cc7 = 0xff;   // no CC7 since boot
		m_volume = 255;
		m_pedal2Down = m_bankLever = m_bankLatch = false;
		m_rpn = m_nrpn = 0x3fff;
		m_nrpnSelected = false;
		m_active = matrix::activeSources(m_edit);
		m_vcfByEnv1 = m_edit.vcfByEnv1();
		m_fmByEnv3 = m_edit.fmByEnv3();
		m_modeEdited = false;

		m_global = GlobalControls();
		m_lastCtrl = 0xff;
		m_lastNoise = -1;
		std::fill(std::begin(m_lastRange), std::end(m_lastRange), -1);

		m_now = 0.0;
		m_writes.clear();
		resetScheduler();
	}

	void Core::midiIn(const double _timeUs, const uint8_t _byte)
	{
		m_inQueue.push_back({_timeUs, _byte});
		queueByteCost(_timeUs, _byte);
	}

	double Core::midiIn(double _timeUs, const std::vector<uint8_t>& _bytes)
	{
		for(const auto b : _bytes)
		{
			midiIn(_timeUs, b);
			_timeUs += ByteUs;
		}
		return _timeUs;
	}

	std::vector<HwWrite> Core::takeWrites()
	{
		std::vector<HwWrite> w(m_writes);
		m_writes.clear();
		return w;
	}

	std::vector<uint8_t> Core::takeMidiOut()
	{
		std::vector<uint8_t> m(m_midiOut);
		m_midiOut.clear();
		return m;
	}

	KeyboardMode Core::effectiveMode() const
	{
		return m_master.unison() ? KeyboardMode::Unison : m_edit.keyboardMode();
	}

	double Core::bootSustainUs(const uint32_t _v)
	{
		// time from the 0D's last byte to the boot leftover sustain jump per voice (env4/speed10 with the fitted load end, -1 = later than 6 s)
		static constexpr double t[VoiceCount] = {96000.0, 1440000.0, 355000.0, -1.0, 2006000.0, 1396000.0};
		return t[_v];
	}

	void Core::loadEditBuffer(const Patch& _p)
	{
		// measure: effect on sounding voices [R-CTRL]
		if(m_lfoBootProgram)
		{
			// the boot program's LFOs ran until the first load began (0D: 275 bytes)
			m_lfoBootProgram = false;
			const double start = m_now - 275 * ByteUs;
			const double steps = start > Lfo::BootStartUs ? std::floor((start - Lfo::BootStartUs) / Lfo::BootTickUs) : 0.0;
			for(auto& vs : m_voices)
				for(auto& l : vs.lfo)
					l.bootProgram(static_cast<int64_t>(std::min(steps, 1e15)));
		}
		m_edit = _p;
		m_modeEdited = false;
		m_active = matrix::activeSources(m_edit);
		m_alloc.setMode(effectiveMode());
		m_patchLoaded = true;
		m_envLoadUs = m_now;
		// opcode 06 edits of P22/P31 do not reach the voices (vcf/ editcheck)
		m_vcfByEnv1 = _p.vcfByEnv1();
		m_fmByEnv3 = _p.fmByEnv3();
	}

	// MIDI

	bool Core::channelMatches(const uint8_t _channel, int& _voice) const
	{
		_voice = -1;
		if(m_master.monoMode())
		{
			const auto offset = (_channel - m_master.channel() + 16) % 16;
			if(offset >= static_cast<int>(VoiceCount))
				return false;
			_voice = offset;
			return true;
		}
		return m_master.omni() || _channel == m_master.channel();
	}

	void Core::handleMessage(const MidiMessage& _m)
	{
		if(_m.status == 0xf0)
		{
			handleSysex(_m.sysex);
			return;
		}
		if(_m.status >= 0xf0)
			return;

		int voice;
		if(!channelMatches(_m.status & 0x0f, voice))
			return;

		switch(_m.status & 0xf0)
		{
		case 0x80:
		case 0x90:
		{
			// [R-PITCH] transposed keys outside 0-127 take no voice
			const int key = _m.data1 + m_master.transpose();
			if(key < 0 || key > 127)
				break;
			const auto note = static_cast<uint8_t>(key);
			if((_m.status & 0xf0) == 0x90 && _m.data2)
			{
				if(voice >= 0) m_alloc.noteOnVoice(static_cast<uint32_t>(voice), note, _m.data2, m_allocEvents);
				else m_alloc.noteOn(note, _m.data2, m_allocEvents);
			}
			else
			{
				const uint8_t rel = (_m.status & 0xf0) == 0x80 ? _m.data2 : 64;   // measure: release velocity of 9n vv=0 [R-CTRL]
				if(voice >= 0) m_alloc.noteOffVoice(static_cast<uint32_t>(voice), note, rel, m_allocEvents);
				else m_alloc.noteOff(note, rel, m_allocEvents);
			}
			break;
		}
		case 0xb0:
			handleController(_m.data1, _m.data2, voice);
			break;
		case 0xc0:
			if(voice <= 0 && m_master.patchChangeEnabled())
				handleProgram(_m.data1);
			break;
		case 0xd0:
			m_pressure = matrix::controller(_m.data1);   // poly pressure (An) is ignored
			break;
		case 0xe0:
			m_bend8 = static_cast<uint8_t>(((_m.data2 << 7) | _m.data1) >> 6);
			m_bend = matrix::bend(m_bend8);
			break;
		default:
			break;
		}
	}

	void Core::handleController(const uint8_t _cc, const uint8_t _value, int)
	{
		switch(_cc)
		{
		case 121:
			m_bend = m_pressure = m_lever2 = m_lever3 = m_pedal1 = m_pedal2 = 0;
			m_bend8 = 128;
			m_pedal2Down = false;
			return;
		case 123: m_alloc.allNotesOff(m_allocEvents); return;
		case 124: m_master.data[12] = 0; return;
		case 125: m_master.data[12] = 1; return;
		case 126: m_master.data[35] = 1; m_alloc.allNotesOff(m_allocEvents); return;
		case 127: m_master.data[35] = 0; m_alloc.allNotesOff(m_allocEvents); return;
		default: break;
		}

		if(!m_master.controllersEnabled())
			return;

		if(_cc == m_master.pedal1Cc()) m_pedal1 = matrix::controller(_value);
		if(_cc == m_master.lever2Cc()) m_lever2 = matrix::controller(_value);
		if(_cc == m_master.lever3Cc()) m_lever3 = matrix::controller(_value);
		if(_cc == m_master.pedal2Cc())
		{
			// pedal 2 is a matrix source and the external trigger; it never holds notes
			m_pedal2 = matrix::controller(_value);
			const bool down = _value >= 64;   // measure: threshold [R-CTRL2]
			if(down != m_pedal2Down)
				externalTrigger(down);
			m_pedal2Down = down;
		}

		switch(_cc)
		{
		case 1:
			m_bankLever = _value >= 32;   // armed at 63-127; threshold below 63 not measured [R-CTRL2]
			break;
		case 31:
			m_bankLatch = _value >= 64;   // 63 and below off, 64 and 127 on (ctrl/, ctrl2/)
			break;
		case 7:
			if(m_master.pedal1Cc() != 7)
			{
				m_cc7 = _value;
				updateVolume();
			}
			break;
		case 6: dataEntry(_value, false); break;
		case 96: dataEntry(1, true); break;
		case 97: dataEntry(-1, true); break;
		case 98: m_nrpn = _value; m_nrpnSelected = true; break;
		case 99: break;   // NRPN MSB is ignored
		case 100: m_rpn = (m_rpn & 0x3f80) | _value; m_nrpnSelected = false; break;
		case 101: m_rpn = (m_rpn & 0x7f) | (_value << 7); m_nrpnSelected = false; break;
		default: break;
		}
	}

	void Core::updateVolume()
	{
		// before any CC7: full volume, silent when inverted (sysex/glob_b170)
		if(m_cc7 > 127)
			m_volume = m_master.volumeInvert() ? 0 : 255;
		else
			m_volume = m_master.volumeInvert() ? 255 - 2 * m_cc7 : 2 * m_cc7;
	}

	void Core::dataEntry(const int _value, const bool _relative)
	{
		if(m_nrpnSelected)
		{
			// CC98 = patch parameter number; CC6 writes value - 64 (P21 unchanged), CC96/97 step by 1 (ctrl/rpn)
			const auto num = static_cast<uint8_t>(m_nrpn & 0x7f);
			const auto* info = findParam(num);
			if(!info)
				return;
			const int cur = static_cast<int8_t>(m_edit.data[info->byte]);
			const int v = _relative ? clampi(cur + _value, -128, 127) : (num == 21 ? _value : _value - 0x40);   // nrpn/incdec, cc6_values
			setParam(m_edit, num, v);
			m_alloc.setMode(effectiveMode());
			return;
		}
		if(m_rpn > 2)
			return;
		applyRegisteredParam(m_rpn, _relative ? -1000 + _value : _value);
		stall(RpnBusyUs);
	}

	void Core::applyRegisteredParam(const int _number, int _value)
	{
		const bool relative = _value < -500;
		const int delta = _value + 1000;
		switch(_number)
		{
		case 0:   // bend range in semitones, no limit (64 and 127 reach the count limits)
			m_master.data[164] = static_cast<uint8_t>(clampi(relative ? m_master.bendRange() + delta : _value, 0, 127));
			break;
		case 1:   // measure: unit of fine tune [R-CTRL]
			m_master.data[8] = static_cast<uint8_t>(static_cast<int8_t>(clampi(relative ? m_master.tune() + delta : _value - 0x40, -64, 63)));
			break;
		case 2:
			m_master.data[34] = static_cast<uint8_t>(static_cast<int8_t>(clampi(relative ? m_master.transpose() + delta : _value - 0x40, -64, 63)));
			break;
		default:
			break;
		}
	}

	void Core::handleProgram(const uint8_t _program)
	{
		// ctrl/program.csv
		if(m_bankLever || m_bankLatch)
		{
			m_bank = _program > 9 ? 9 : _program;
			m_master.setBankLock(true);
			scheduleSelect(m_bank * 100 + m_current % 100, ProgramLoadUs);
			return;
		}
		if(m_master.bankLock())
		{
			scheduleSelect(m_bank * 100 + (_program > 99 ? 99 : _program), ProgramLoadUs);
			return;
		}
		scheduleSelect(_program, ProgramLoadUs);
	}

	void Core::selectPatch(const uint32_t _number)
	{
		m_current = _number % PatchCount;
		m_bank = m_current / 100;
		loadEditBuffer(m_patches[m_current]);
	}

	void Core::scheduleSelect(const uint32_t _number, const double _delayUs)
	{
		// the load keeps the controller busy (input waits), then cuts every sounding voice
		if(m_mode == OutputMode::Vintage)
			m_programLoadAt = m_cpu + _delayUs;
		cutVoices();
		selectPatch(_number);
	}

	double Core::editCostUs(const uint8_t _param, const bool _portaOn, const bool _unison)
	{
		// opcode 06 recompute time per parameter group: alone from edit/, inside the porta setup chains
		// from edit2/ (leave one out): with portamento on each recompute costs 3.6 ms more, the rate
		// edit only that; the DCO routing edit costs 10 ms more in unison
		double us = 0.0;
		switch(_param)
		{
		case 8: case 18: return 67400.0 + (_unison ? 10000.0 : 0.0);       // DCO keyboard/portamento routing
		case 26: case 45: case 46: case 47: us = 63400.0; break;          // VCF keyboard, portamento modes
		case 0: case 10: case 2: case 5: case 15: us = 9300.0; break;     // DCO frequency, sync, shape (dco/ audio_shape1, audio_sync)
		case 16: return 67400.0;                                          // DCO2 wave select (dco/audio_select2)
		case 6: us = 63700.0; break;                                      // DCO1 wave select (dco/audio_pw1: queued in a patch load, +66.6 ms)
		case 3: case 13: us = 4500.0; break;                              // pulse width (dco/audio_pw1)
		case 20: us = 3900.0; break;                                      // mix (dco/audio_mix)
		case 48: us = 26800.0; break;                                     // keyboard mode (VCA2 bursts, edit/p48_2)
		case 44: break;                                                   // rate
		default:
			if(_param >= 50 && _param <= 79) us = 15600.0;                // envelopes (P64 measured)
			else return 0.0;
			break;
		}
		return us + ((_portaOn || (_unison && _param != 44 && _param != 48)) ? PortaOnEditUs : 0.0);
	}

	void Core::stall(const double _us)
	{
		if(m_mode == OutputMode::Vintage && _us > 0.0)
		{
			work(_us);
			m_stalled = true;
		}
	}

	void Core::cutVoices()
	{
		// notes are cut, not released, and do not come back (voice/); the patch load writes VCA2 0
		m_alloc.allNotesOff(m_allocEvents);
		m_allocEvents.clear();
		for(auto& vs : m_voices)
		{
			for(auto& e : vs.env)
				e.reset();
			vs.gate = false;
		}
	}

	void Core::storePatch(const uint32_t _number, const Patch& _p)
	{
		// the clean build has no factory ROM: every bank is writable, memory protect covers all
		if(m_master.memoryProtect())
			return;
		m_patches[_number % PatchCount] = _p;
	}

	void Core::sendPatch(const Patch& _p, const uint8_t _bank, const uint8_t _number)
	{
		encodePatchReply(_p, _bank, _number, m_midiOut);
	}

	void Core::sendMaster()
	{
		// byte 165: bank lock in the MSB, current bank in the low bits (sysex/req0_bank2)
		auto m = m_master.data;
		m[165] = static_cast<uint8_t>((m[165] & 0x80) | (m_bank % 10));
		static constexpr uint8_t head[] = {0xf0, 0x10, 0x06, 0x03, 0x03};
		m_midiOut.insert(m_midiOut.end(), std::begin(head), std::end(head));
		packNibbles(m.data(), Master::Size, m_midiOut);
		m_midiOut.push_back(0xf7);
	}

	void Core::sendBankDump()
	{
		// 100 singles, 80 dummy splits numbered 79 down to 0 (each followed by a lone F7), master
		for(uint32_t i = 0; i < 100; ++i)
			sendPatch(m_patches[m_bank * 100 + i], static_cast<uint8_t>(m_bank), static_cast<uint8_t>(i));
		static constexpr uint8_t split[18] = {0x4e, 0x4f, 0x53, 0x50, 0x4c, 0x54, 0x20, 0x20, 0x3b, 0x00, 0x01, 0x3c, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
		for(int n = 79; n >= 0; --n)
		{
			const uint8_t head[] = {0xf0, 0x10, 0x06, 0x02, static_cast<uint8_t>(n)};
			m_midiOut.insert(m_midiOut.end(), std::begin(head), std::end(head));
			packNibbles(split, sizeof(split), m_midiOut);
			m_midiOut.push_back(0xf7);
			m_midiOut.push_back(0xf7);
		}
		sendMaster();
	}

	void Core::handleSysex(const std::vector<uint8_t>& _s)
	{
		if(_s.size() >= 6 && _s[1] == 0x7e && _s[3] == 0x06 && _s[4] == 0x01)
		{
			if(_s[2] == 0x7f || _s[2] == m_master.channel())
			{
				const uint8_t r[] = {0xf0, 0x7e, m_master.channel(), 0x06, 0x02, 0x10, 0x06, 0x00, 0x02, 0x00, ' ', '1', '2', '0', 0xf7};
				m_midiOut.insert(m_midiOut.end(), std::begin(r), std::end(r));
			}
			return;
		}
		if(_s.size() >= 13 && _s[1] == 0x7f && _s[3] == 0x02 && _s[4] == 0x01)
		{
			if((_s[2] == 0x7f || _s[2] == 0) && _s[10] <= 9)
			{
				m_bank = _s[10];
				m_master.setBankLock(true);
				scheduleSelect(m_bank * 100 + m_current % 100, ProgramLoadUs);
			}
			return;
		}

		if(_s.size() < 5 || _s[1] != 0x10 || _s[2] != 0x06)
			return;

		const auto op = _s[3];
		constexpr size_t patchMsg = 5 + Patch::Size * 2 + 2;
		switch(op)
		{
		case 0x01:
		case 0x0d:
		{
			// 275 bytes; tolerate a missing number byte padding or trailing bytes from other tools
			if(_s.size() < patchMsg)
				return;
			Patch p;
			if(!unpackNibbles(&_s[5], Patch::Size, p.data.data(), _s[patchMsg - 2]))
				return;   // measure: bad checksum handling [R-SYSEX]
			if(op == 0x0d)
			{
				loadEditBuffer(p);
				return;
			}
			const uint32_t n = m_bank * 100 + (_s[4] % 100);
			const bool stored = !m_master.memoryProtect();
			storePatch(n, p);
			if(stored && n == m_current)
				scheduleSelect(n, StoreRecallUs);   // recalled into the edit buffer, sounding notes cut
			break;
		}
		case 0x03:
		{
			constexpr size_t masterMsg = 5 + Master::Size * 2 + 2;
			if(_s.size() < masterMsg)
				return;
			std::array<uint8_t, Master::Size> m{};
			if(unpackNibbles(&_s[5], Master::Size, m.data(), _s[masterMsg - 2]))
			{
				m_master.receive(m);
				updateVolume();
				m_alloc.setMode(effectiveMode());
			}
			break;
		}
		case 0x04:
		{
			if(_s.size() < 6)
				return;
			const uint8_t num = _s.size() >= 7 ? _s[5] : 0;
			switch(_s[4])
			{
			case 0: sendBankDump(); break;
			case 1:
				if(num < 100)
					sendPatch(m_patches[m_bank * 100 + num], static_cast<uint8_t>(m_bank), num);
				break;
			case 3: sendMaster(); break;
			case 4: sendPatch(m_edit, static_cast<uint8_t>(m_bank), static_cast<uint8_t>(m_current % 100)); break;
			default: break;
			}
			break;
		}
		case 0x06:
			if(_s.size() >= 7)
			{
				applyParam(m_edit, _s[4], _s[5]);
				m_alloc.setMode(effectiveMode());
				const bool porta = (m_edit.dco(0).portaKeyb & 1) || (m_edit.dco(1).portaKeyb & 1) || (m_edit.vcfKeyb() & 1);
				// the unison extras follow a keyboard mode edit, not a unison patch (porta5/ pvp, bus vs edit2/)
				if(_s[4] == 48)
					m_modeEdited = true;
				stall(editCostUs(_s[4], porta, m_modeEdited && effectiveMode() == KeyboardMode::Unison));
			}
			break;
		case 0x0a:
			if(_s.size() >= 6 && _s[4] <= 9)
			{
				// selects the same patch number in the new bank (ctrl/program.csv)
				m_master.setBankLock(true);
				m_bank = _s[4];
				scheduleSelect(m_bank * 100 + m_current % 100, ProgramLoadUs);
			}
			break;
		case 0x0b:
			if(_s.size() >= 9 && _s[4] < Patch::MatrixBuses)
			{
				const int amount = static_cast<int8_t>(static_cast<uint8_t>((_s[6] & 0x7f) | ((_s[6] & 0x40) << 1)));
				const bool del = _s[5] == 0 || _s[7] == 0;
				m_edit.setBus(_s[4], del ? 0 : std::min<uint8_t>(_s[5], 20), del ? 0 : clampi(amount, -63, 63), del ? 0 : std::min<uint8_t>(_s[7], 32));
				m_active = matrix::activeSources(m_edit);
				const bool porta = (m_edit.dco(0).portaKeyb & 1) || (m_edit.dco(1).portaKeyb & 1) || (m_edit.vcfKeyb() & 1);
				stall(68200.0 + (porta ? PortaOnEditUs : 0.0));   // edit2/porta2_bus without the 0B
			}
			break;
		case 0x0c:
			// unlocked: programs 0-99 select bank 0, 100-127 bank 1 (ctrl/program.csv)
			m_master.setBankLock(false);
			break;
		case 0x0e:
			if(_s.size() >= 8 && _s[5] <= 9)
				storePatch(_s[5] * 100u + _s[4] % 100, m_edit);
			break;
		default:
			break;
		}
	}

	// voices

	void Core::processAllocEvents()
	{
		for(const auto& e : m_allocEvents)
		{
			switch(e.type)
			{
			case VoiceAllocator::Event::Type::On: noteOnVoice(e.voice, e.note, e.velocity, e.legato, false, e.voiceGated); break;
			case VoiceAllocator::Event::Type::Retarget: noteOnVoice(e.voice, e.note, e.velocity, e.legato, true, e.voiceGated); break;
			case VoiceAllocator::Event::Type::Off: noteOffVoice(e.voice, e.velocity); break;
			}
		}
		m_allocEvents.clear();
	}

	namespace
	{
		Envelope::Params envParams(const Patch& _p, const std::array<Mod, static_cast<size_t>(Dest::Count)>& _dest, const uint32_t _i)
		{
			const auto e = _p.env(_i);
			const size_t d0 = di(Dest::Env1Delay) + _i * 5;
			Envelope::Params ep;
			// _dest: matrix offsets of the stages (knee table, matrix.h)
			ep.delay = e.delay;
			ep.attack = e.attack;
			ep.decay = e.decay;
			ep.release = e.release;
			ep.delayMod = _dest[d0];
			ep.attackMod = _dest[d0 + 1];
			ep.decayMod = _dest[d0 + 2];
			ep.releaseMod = _dest[d0 + 3];
			ep.sustain = e.sustain;
			ep.trigger = e.trigger;
			ep.lfoTrigger = e.lfoTrigger;
			ep.mode = e.mode;
			return ep;
		}

		// LFO: 7 bit view of a Mod source (unipolar() inverse), sampled source as t
		// 7 bit view of a controller (matrix level s = 2 * value) and the sampled source as t
		int source7(const Mod _v) { return matrix::sourceLevel(_v) >> 1; }
		int sampleT(const Mod _v) { const int s = matrix::sourceLevel(_v); return s >= 0 ? clampi(s, 0, 255) : clampi(s + 128, 0, 255); }   // bipolar: guess

		Lfo::Params lfoParams(const Patch& _p, const uint32_t _i, const int _speed, const int _sampleT, const int _amp)
		{
			const auto l = _p.lfo(_i);
			Lfo::Params lp;
			lp.speed = clampi(_speed, 0, 63);
			lp.wave = static_cast<uint8_t>(l.wave);
			lp.trigger = l.trigger;
			lp.retrigger = l.retrigger;
			lp.lag = l.lag;
			lp.sampleT = _sampleT;
			lp.amp = _amp;
			return lp;
		}
	}

	int32_t Core::keyPitch(const uint32_t _v) const
	{
		return m_voices[_v].note * 256;   // transpose is applied on note input
	}

	void Core::noteOnVoice(const uint32_t _v, const uint8_t _note, const uint8_t _velocity, const bool _legato, const bool _retarget, const bool _voiceGated)
	{
		auto& vs = m_voices[_v];
		vs.note = _note;
		vs.velocity = _velocity;
		vs.gate = true;

		// legato: glide only while the voice is still gated (porta/)
		vs.porta.setTarget(keyPitch(_v), !m_edit.legato() || _voiceGated);

		for(uint32_t i = 0; i < 3; ++i)
			vs.env[i].gateOn(envParams(m_edit, vs.dest, i), _legato || _retarget);
		for(uint32_t i = 0; i < 2; ++i)
		{
			vs.lfo[i].keyTrigger(lfoParams(m_edit, i, 0, 0, 0), _retarget);
			vs.ramp[i].keyTrigger(m_edit.rampTrigger(i), _legato);
		}
	}

	void Core::noteOffVoice(const uint32_t _v, const uint8_t _velocity)
	{
		auto& vs = m_voices[_v];
		vs.gate = false;
		vs.releaseVelocity = _velocity;
		for(uint32_t i = 0; i < 3; ++i)
			vs.env[i].gateOff(envParams(m_edit, vs.dest, i));
	}

	void Core::externalTrigger(const bool _down)
	{
		if(_down)
			m_lfoExtPending = true;   // LFOs restart from the next pass of the service list (lfo/trig3)
		m_envExtPending.push_back(_down);   // envelopes and ramps too (env2/ext_trig*)
	}

	void Core::applyEnvExternal()
	{
		for(const bool down : m_envExtPending)
		{
			for(auto& vs : m_voices)
			{
				// pedal 2 gates the envelopes of all voices in external trigger modes (env/ ext_trig)
				for(uint32_t i = 0; i < 3; ++i)
					vs.env[i].externalGate(envParams(m_edit, vs.dest, i), down);
				if(!down)
					continue;
				for(uint32_t i = 0; i < 2; ++i)
					vs.ramp[i].externalTrigger(m_edit.rampTrigger(i), vs.gate);
			}
		}
		m_envExtPending.clear();
	}

	void Core::tickModulation(const double _dtUs)
	{
		tickGlobal(_dtUs);
		for(uint32_t v = 0; v < VoiceCount; ++v)
			tickVoice(v, _dtUs);
	}

	void Core::tickGlobal(const double _dtUs)
	{
		applyEnvExternal();
		if(m_lfoExtPending)
		{
			m_lfoExtPending = false;
			for(auto& vs : m_voices)
				for(uint32_t i = 0; i < 2; ++i)
					vs.lfo[i].externalTrigger(lfoParams(m_edit, i, 0, 0, 0));
		}

		// Vintage steps the vibrato once per voice block, in computeSources
		if(m_mode != OutputMode::Vintage)
		{
			latchVibrato();
			vibratoStep(_dtUs / RefreshUs * VoiceCount);
		}
	}

	void Core::vibratoStep(const double _blocks)
	{
		m_vibratoValue = m_vibrato.tick(m_vibLatch[0], static_cast<uint8_t>(m_vibLatch[1]), m_vibLatch[2], _blocks);
	}

	void Core::latchVibrato()
	{
		auto vibSrc = [this](const uint8_t _code)
		{
			const int i = Vibrato::sourceIndex(_code);
			return i == 0 ? 0 : source7(i == 1 ? m_lever2 : m_pedal1);
		};
		const int speed = m_master.vibSpeed() + lfoCurve::modOffset(vibSrc(m_master.vibSpeedModSource()), m_master.vibSpeedModAmount());
		const int amp = lfoCurve::clampMul(lfoCurve::ampBase(m_master.vibAmp()) + lfoCurve::ampMod(2 * vibSrc(m_master.vibAmpModSource()), m_master.vibAmpModAmount()));
		m_vibLatch[0] = clampi(speed, 0, 63);
		m_vibLatch[1] = m_master.vibWave();
		m_vibLatch[2] = amp;
	}

	void Core::tickVoice(const uint32_t _v, const double _dtUs)
	{
		computeSources(_v, _dtUs);
		computeControls(_v);
	}

	void Core::computeSources(const uint32_t _v, const double _dtUs)
	{
		using matrix::Curve;
		auto& vs = m_voices[_v];
		auto& src = vs.src;
		const auto& p = m_edit;
		auto lvl = [&](const Source _s) { return matrix::sourceLevel(src[si(_s)]); };

		// LFOs and vibrato move once per voice block in Vintage, by elapsed time in Smooth
		const double lfoBlocks = m_mode == OutputMode::Vintage ? 1.0 : _dtUs / RefreshUs;
		const bool vintage = m_mode == OutputMode::Vintage;
		const double envDt = vintage ? envCurve::StepUs : _dtUs;   // one env/ramp step per block
		if(m_mode == OutputMode::Vintage)
		{
			// settings reach the vibrato one block late (vibamt/: a CC lands on the second block
			// after it, vib/: the 03 globals on the second block after the stall)
			vibratoStep(1.0);
			latchVibrato();
		}

		// controller and key sources first, the modulators may use them
		const Mod keyboard = matrix::keyboard(vs.note);
		// LFO speed by keyboard: 7 bit value 2 * (note - 36); idle voices act as 42 (lfo/speedbykey_*, sample_src9)
		const int keyboard7 = vs.gate || vs.velocity ? clampi(2 * (vs.note - 36), 0, 127) : 42;
		src[si(Source::Keyboard)] = keyboard;
		src[si(Source::KeyGate)] = vs.gate ? matrix::KeyGateOn : 0;
		src[si(Source::Velocity)] = matrix::controller(vs.velocity);
		src[si(Source::ReleaseVelocity)] = matrix::controller(vs.releaseVelocity);
		src[si(Source::Pressure)] = m_pressure;
		src[si(Source::Pedal1)] = m_pedal1;
		src[si(Source::Pedal2)] = m_pedal2;
		src[si(Source::Lever1)] = m_bend;
		src[si(Source::Lever2)] = m_lever2;
		src[si(Source::Lever3)] = m_lever3;
		src[si(Source::Vibrato)] = m_vibratoValue;

		// the buses read the ramps as they were before this block (ramp/ step timing); LFO buses and
		// fixed routes read the same value (matrix/ fixed_routes: +63 fixed and -63 bus cancel)
		// sources the patch does not use are not computed and keep their last value
		const std::array<Envelope, 3> envBefore = vs.env;   // an LFO1 event redoes this block's step
		for(uint32_t i = 0; i < 3; ++i)
		{
			if(!m_active.env[i])
				continue;
			const auto ep = envParams(p, vs.dest, i);
			const auto e = p.env(i);
			vs.env[i].tick(ep, envDt, vintage);
			// amplitude multiplier 0..255; matrix offset in multiplier steps (dest in 1/512)
			const int mul = envCurve::ampMultiplier(e.amp, vs.velocity, e.ampByVelocity, vs.dest[di(Dest::Env1Amp) + i * 5]);
			src[si(Source::Env1) + i] = envCurve::output(vs.env[i].level(), mul);
		}

		for(uint32_t i = 0; i < 2; ++i)
		{
			if(m_active.ramp[i])
			{
				vs.ramp[i].tick(p.rampRate(i), envDt, vintage);
				src[si(Source::Ramp1) + i] = (vs.ramp[i].level() >> 8) * 64;   // matrix level = high byte (ramp/)
			}
		}

		for(uint32_t i = 0; i < 2; ++i)
		{
			if(!m_active.lfo[i])
				continue;
			const auto l = p.lfo(i);
			const int speedSrc = i == 0 ? source7(m_pressure) : keyboard7;
			const int base = m_lfoSpeedOverride[i] >= 0 ? m_lfoSpeedOverride[i] : l.speed;
			const int speed = base + lfoCurve::modOffset(speedSrc, l.speedMod) + vs.dest[i == 0 ? di(Dest::Lfo1Speed) : di(Dest::Lfo2Speed)];   // offset in speed steps (matrix2/)
			// multiplier: patch amplitude + ramp (8 bit level) + matrix, clamped once (lfoamp/)
			const int ampMul = lfoCurve::clampMul(lfoCurve::ampBase(l.amp) + lfoCurve::ampMod(lfoCurve::rampLevel(vs.lfoRamp[i]), l.ampByRamp) + vs.dest[i == 0 ? di(Dest::Lfo1Amp) : di(Dest::Lfo2Amp)]);
			src[si(Source::Lfo1) + i] = vs.lfo[i].tick(lfoParams(p, i, speed, (l.sampleSource == Source::None ? Lfo::sourceZeroT(_v) : sampleT(src[si(l.sampleSource)])), ampMul), lfoBlocks);
			vs.lfoRamp[i] = vs.ramp[i].level();   // amp by ramp sees it one block late (lfoamp/ampbyramp_*_rate30)
		}

		if(m_active.lfo[0])
		{
			// envelope LFO trigger: LFO1 top triggers, bottom releases
			const bool rising = vs.lfo[0].rising();
			const bool wrapped = vs.lfo[0].wrapped();
			const bool half = !wrapped && rising && !vs.lfo1Rising;
			vs.lfo1Rising = rising;
			const double sinceLoad = (vs.lastTickUs < 0.0 ? m_now : vs.lastTickUs + _dtUs) - m_envLoadUs;   // block start (Vintage)
			if(m_envLoadUs >= 0.0 && bootSustainUs(_v) >= 0.0 && sinceLoad >= bootSustainUs(_v))
				for(uint32_t i = 0; i < 3; ++i)
					if(vs.env[i].bootArmed())
					{
						vs.env[i].bootSustain(envParams(p, vs.dest, i));
						const auto e = p.env(i);
						src[si(Source::Env1) + i] = envCurve::output(vs.env[i].level(), envCurve::ampMultiplier(e.amp, vs.velocity, e.ampByVelocity, vs.dest[di(Dest::Env1Amp) + i * 5]));
					}
			// the envelope steps from the new stage in the same block as the LFO1 top/bottom (env4/: attack 93 in the top's block)
			if(wrapped || half)
				for(uint32_t i = 0; i < 3; ++i)
				{
					const auto ep = envParams(p, vs.dest, i);
					Envelope redo = envBefore[i];
					if(!vs.env[i].lfoEvent(ep, wrapped, half, vs.gate) || m_mode != OutputMode::Vintage || !m_active.env[i])
						continue;
					redo.lfoEvent(ep, wrapped, half, vs.gate);
					redo.tick(ep, envDt, vintage);
					vs.env[i] = redo;
					const auto e = p.env(i);
					src[si(Source::Env1) + i] = envCurve::output(vs.env[i].level(), envCurve::ampMultiplier(e.amp, vs.velocity, e.ampByVelocity, vs.dest[di(Dest::Env1Amp) + i * 5]));
				}
		}

		// measure: velocity and matrix scaling of the rate [R-PORTA2]
		// porta2/, porta3/: with velocity or bus modulation the base is twice the rate and the unison spread is gone
		bool portaMod = p.portaByVelocity() != 0;
		int portaRate = Portamento::rateOffset(Portamento::velocitySource(vs.velocity), p.portaByVelocity());
		for(uint32_t b = 0; b < Patch::MatrixBuses; ++b)
		{
			const auto bus = p.bus(b);
			if(bus.dest != Dest::PortaTime || bus.source == Source::None)
				continue;
			portaMod = true;
			portaRate += Portamento::rateOffset(static_cast<int>((static_cast<int64_t>(src[si(bus.source)]) * 127 + ModFull / 2) / ModFull), bus.amount);   // bipolar sources [R-PORTA2]
		}
		uint16_t coeff = Portamento::coefficient(portaMod ? Portamento::modulatedRate(p.portaRate(), portaRate) : p.portaRate());
		if(!portaMod && effectiveMode() == KeyboardMode::Unison)
			coeff = Portamento::unisonCoefficient(coeff, _v);
		// the glide only runs while something follows it (porta4/: boot leftovers stay until DCO1 porta is on)
		bool portaUsed = (p.dco(0).portaKeyb & 1) || (p.dco(1).portaKeyb & 1) || (p.vcfKeyb() & 1);
		for(uint32_t b = 0; b < Patch::MatrixBuses && !portaUsed; ++b)   // a bus reading it also runs it (matrix2/porta_source)
			portaUsed = p.bus(b).source == Source::Portamento && p.bus(b).dest != Dest::None;
		const int32_t porta = portaUsed ? vs.porta.tick(coeff, p.lagMode(), _dtUs, m_mode == OutputMode::Vintage) : vs.porta.value();

		// measure: portamento source scale, note 127 read 0 [R-MATRIX2]
		// 4 per semitone above note 48; past 255 (note 112) it reads 0 (matrix2/porta_source)
		const int32_t portaLevel = (porta - 48 * 256) >> 6;
		src[si(Source::Portamento)] = (portaLevel < 0 || portaLevel > 255 ? 0 : portaLevel) * 64;
		std::array<uint8_t, 5> points;
		for(uint32_t i = 0; i < 5; ++i)
			points[i] = p.trackPoint(i);
		src[si(Source::Tracking)] = trackingGenerator(src[si(p.trackInput())], bipolarDiv(p.trackInput()), points);

		vs.busSrc = src;   // ramps as stepped in this block: a note on reset shows in its restart block (ramp/rate_*)

		// time, rate and amplitude destinations: offsets in 1/256 parameter steps for the next tick
		vs.dest.fill(0);
		for(size_t d = di(Dest::Env1Delay); d <= di(Dest::PortaTime); ++d)
		{
			const auto dest = static_cast<Dest>(d);
			// env amplitude: in amplitude multiplier steps (matrix2/); time and rate destinations [R-MATRIX2]
			const bool envStage = d >= di(Dest::Env1Delay) && d <= di(Dest::Env3Amp);
			const bool lfoSpeed = dest == Dest::Lfo1Speed || dest == Dest::Lfo2Speed;
			const bool lfoAmp = dest == Dest::Lfo1Amp || dest == Dest::Lfo2Amp;   // LFO amp multiplier, same curve as ramp and CC amp mods (lfoamp/, vibamt/) [R-MATRIX2]
			vs.dest[d] = envStage || lfoAmp ? matrix::busSum(p, vs.busSrc, dest, Curve::EnvAmp)
				: lfoSpeed ? matrix::busSum(p, vs.busSrc, dest, Curve::Unit) : matrix::busSum(p, vs.busSrc, dest, Curve::Exp) / 2;
		}
	}

	void Core::computeControls(const uint32_t _v)
	{
		using matrix::Curve;
		auto& vs = m_voices[_v];
		const auto& p = m_edit;
		const auto& src = vs.src;
		auto& c = vs.controls;
		auto lvl = [&](const Source _s) { return matrix::sourceLevel(src[si(_s)]); };

		const int32_t key = keyPitch(_v);
		const int32_t porta = vs.porta.value();
		const int range = m_master.bendRange();
		const int32_t vib = m_vibratoValue / lfoCurve::toMod(1) * 4;   // 4/256 semitone per vibrato level, ±2 semitones (vib/ counts)

		for(uint32_t d = 0; d < 2; ++d)
		{
			const auto o = p.dco(d);
			// [R-PITCH]: DCO1 plays porta or keyboard, DCO2 porta, keyboard or fixed key 24
			const Dest dFreq = d == 0 ? Dest::Dco1Freq : Dest::Dco2Freq;
			const Dest dPw = d == 0 ? Dest::Dco1Pw : Dest::Dco2Pw;
			const Dest dShape = d == 0 ? Dest::Dco1Shape : Dest::Dco2Shape;
			const int32_t key256 = (o.portaKeyb & 1) ? porta : ((d == 0 || (o.portaKeyb & 2)) ? key : dco::FixedKey * 256);
			// key + frequency (floorPitch), then bend and modulation held at 0 below the table, then
			// master tune, detune and the unison spread, which wrap to the top (count 31) below 0
			int32_t base = dco::floorPitch(dco::keyPitch(key256, static_cast<int8_t>(o.freq)));
			base += matrix::busSum(p, vs.busSrc, dFreq, Curve::Pitch);
			base += matrix::routeFine(src[si(Source::Lfo1)], o.lfo1Freq, Curve::Pitch);
			if(o.levers & 1) base += dco::bendFine(m_bend8 << 6, range & 127);   // 8 bit bend; sysex/glob_b164_v128: range 128 bends nothing
			if(o.levers & 2) base += vib;
			base = (base < 0 ? 0 : base) + dco::tuneFine(m_master.tune());
			if(d == 1)
				base += dco::detuneFine(p.detune()) + (effectiveMode() == KeyboardMode::Unison ? dco::unisonSpread(_v) : 0);
			c.pitch[d] = base;

			// shape and pulse width accumulate in 1/8 code (matrix.h), handed over in 1/256 steps
			// matrix/: modulated shape is not held to 0-63 (cvDco.cpp clamps it to -2.05..65.05)
			c.shape[d] = (o.shape & 127) * 256;   // sysex/edit_p05: 06 data 64 and 127 stay (shapeTop)
			for(uint32_t b = 0; b < Patch::MatrixBuses; ++b)
			{
				const auto bus = p.bus(b);
				if(bus.dest == dShape && bus.source != Source::None)
					c.shape[d] += dco::shapeRoute(matrix::sourceLevel(vs.busSrc[si(bus.source)]), bus.amount);
			}
			c.shape[d] = dco::shapeTop(c.shape[d]);
			int32_t pw = matrix::applyBuses(p, vs.busSrc, dPw, matrix::basePw(o.pw), Curve::Exp);
			pw = matrix::sat(pw, matrix::route(lvl(Source::Lfo2), o.lfo2Pw, Curve::Exp));
			c.pw[d] = pw - 512;   // 1/512 steps: the accumulator unit (cvDco::pulseWidth)
			c.pulse[d] = dco::pulseOn(d, o.wave);
		}

		c.mix = (matrix::code(matrix::applyBuses(p, vs.busSrc, Dest::Mix, matrix::baseMix(p.mix()), Curve::Exp)) - 32) * 4;   // balance code = 32 + mix / 4

		cvFilterAmp::Inputs fa;
		fa.voice = _v;
		fa.keyPitch = key;
		fa.portaPitch = porta;
		fa.env1 = src[si(Source::Env1)];
		fa.env2 = src[si(Source::Env2)];
		fa.env3 = src[si(Source::Env3)];
		fa.pressure = m_pressure;
		fa.velocity = vs.velocity;
		fa.bend = m_bend;
		fa.bend8 = m_bend8;
		fa.bendRange = m_master.bendRange();
		fa.fineTune = dco::tuneFine(m_master.tune());   // RPN 1 / master tune: the VCF moves half the pitch offset (ctrl/rpn)
		fa.vibrato = m_vibratoValue;
		// buses in matrix order, u16 = 2 x the 1/8 code accumulator unit
		auto buses = [&](const Dest _d, const Curve _c, cvFilterAmp::Buses& _b)
		{
			_b.n = 0;
			for(uint32_t b = 0; b < Patch::MatrixBuses; ++b)
			{
				const auto bus = p.bus(b);
				if(bus.dest == _d && bus.source != Source::None)
					_b.v[_b.n++] = 2 * matrix::route(matrix::sourceLevel(vs.busSrc[si(bus.source)]), bus.amount, _c);
			}
		};
		buses(Dest::VcfFreq, Curve::Exp, fa.busVcf);
		buses(Dest::VcfRes, Curve::Exp, fa.busRes);
		buses(Dest::VcfFm, Curve::Exp, fa.busFm);
		buses(Dest::Vca1, Curve::Linear, fa.busVca1);
		buses(Dest::Vca2, Curve::Exp, fa.busVca2);
		fa.volume = static_cast<uint8_t>(m_volume);
		fa.vcfByEnv1 = m_vcfByEnv1;
		fa.fmByEnv3 = m_fmByEnv3;
		cvFilterAmp::compute(p, fa, c);

		for(uint32_t d = 0; d < 2; ++d)
			vs.timer[d] = cvMap::dcoTimer(_v, d, c.pitch[d], m_global);
	}

	// hardware writes

	void Core::emit(const double _time, const Target _t, const uint16_t _channel, const uint32_t _value)
	{
		m_writes.push_back({_time, _t, _channel, _value});
	}

	// persistence

	namespace
	{
		constexpr uint8_t StateMagic[4] = {'M', 'X', 'C', 'S'};
		constexpr uint8_t StateVersion = 1;
		constexpr size_t StateSize = 4 + 1 + Core::PatchCount * Patch::Size + Master::Size + 2 + 2 + Patch::Size;
	}

	size_t Core::stateSize()
	{
		return StateSize;
	}

	std::vector<uint8_t> Core::getState() const
	{
		std::vector<uint8_t> s;
		getState(s);
		return s;
	}

	void Core::getState(std::vector<uint8_t>& _dst) const
	{
		_dst.clear();
		_dst.reserve(StateSize);
		_dst.insert(_dst.end(), std::begin(StateMagic), std::end(StateMagic));
		_dst.push_back(StateVersion);
		for(const auto& p : m_patches)
			_dst.insert(_dst.end(), p.data.begin(), p.data.end());
		_dst.insert(_dst.end(), m_master.data.begin(), m_master.data.end());
		_dst.push_back(static_cast<uint8_t>(m_bank));
		_dst.push_back(0);
		_dst.push_back(static_cast<uint8_t>(m_current & 0xff));
		_dst.push_back(static_cast<uint8_t>(m_current >> 8));
		_dst.insert(_dst.end(), m_edit.data.begin(), m_edit.data.end());
	}

	bool Core::setState(const std::vector<uint8_t>& _state)
	{
		if(_state.size() < StateSize || !std::equal(std::begin(StateMagic), std::end(StateMagic), _state.begin()) || _state[4] != StateVersion)
			return false;
		size_t i = 5;
		for(auto& p : m_patches)
		{
			std::copy_n(&_state[i], Patch::Size, p.data.begin());
			i += Patch::Size;
		}
		std::copy_n(&_state[i], Master::Size, m_master.data.begin());
		i += Master::Size;
		m_bank = _state[i] % 10;
		i += 2;
		m_current = (_state[i] | (_state[i + 1] << 8)) % PatchCount;
		i += 2;
		Patch edit;
		std::copy_n(&_state[i], Patch::Size, edit.data.begin());
		m_alloc.allNotesOff(m_allocEvents);
		processAllocEvents();
		loadEditBuffer(edit);
		// the restored master block: volume invert, vibrato settings
		updateVolume();
		latchVibrato();
		return true;
	}

	// log format

	const char* targetName(const Target _t)
	{
		switch(_t)
		{
		case Target::Cv: return "cv";
		case Target::Mux: return "mux";
		case Target::Pit: return "pit";
		case Target::DcoMode: return "dcomode";
		case Target::Dco: return "dco";
		case Target::Range: return "range";
		case Target::Noise: return "noise";
		case Target::Ctrl: return "ctrl";
		case Target::Idle: return "idle";
		case Target::Latch: return "latch";
		}
		return "?";
	}

	std::string toCsvLine(const HwWrite& _w)
	{
		char buf[96];
		snprintf(buf, sizeof(buf), "%.1f,%s,%u,%u", _w.timeUs, targetName(_w.target), _w.channel, _w.value);
		return buf;
	}
}
