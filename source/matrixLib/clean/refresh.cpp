// Output scheduler: refresh blocks, service order, DCO restarts, patch load sequence.
// Numbers from measurements/timing/ and init/ (see README.md "Refresh").

#include "core.h"
#include "cvDco.h"
#include "analog.h"

#include <algorithm>
#include <cmath>

namespace matrixClean
{
	namespace
	{
		int64_t ticks(const double _us) { return static_cast<int64_t>(std::llround(_us * 2.0)); }

		// normal block of voice 0, write offsets in us from the vca1 write
		enum Step { SVca1, SVca2a, SDco1, SRange1, SWs1, SPw1, SDco2, SRange2, SWs2, SPw2, SVcf, SFm, SRes, SBal, SVca2b, SCount };
		constexpr double BlockOffsets[SCount] = {0, 134, 428, 526.5, 528.5, 621, 905, 1001, 1003, 1095.5, 1230, 1294, 1337.5, 1380.5, 1419.5};
		// per voice delay of the DCO1 part and of everything from DCO2 on
		constexpr double Shift1[VoiceCount] = {0, 10.5, 0, 11.5, 1, 11.5};
		constexpr double Shift2[VoiceCount] = {0, 21, 0, 23, 2, 23};

		constexpr double MuxLeadWs = 15.0;      // before a waveshape write (range latch in between)
		constexpr double PollAfter = 307.0;     // MIDI poll after the block's last write
		constexpr double GapAfter = 595.35;     // last write to the next block
		constexpr double GapAfterNote = 607.0;
		constexpr double PassCostNoAmpMod = 17.0;
		constexpr double VibRandomCost = 33.0;   // vibrato random wave (vib/wave_4)
		constexpr double VibSpeedModCost = 234.0; // vibrato speed modulation on (vib/smod_*, vibamt/smod*)
		constexpr double PassCost = 73.0;      // extra time once per pass, before the block of m_passVoice
		constexpr double NotePrepCost = 288.0;  // before a note on restart block
		constexpr double NotePrepShortCost = 47.0;
		// per block cost of the voice's modulators (lfo/ jobs, least squares over the patches)
		constexpr double LfoCost = 40.0;        // LFO with amplitude or amp by ramp
		constexpr double LfoRampCost = 64.0;
		constexpr double LfoSpeedModCost = 39.0;
		// per LFO and block against the triangle (lfo/ wave<w>_s30, trig<t>, lag<l>: median cycles)
		constexpr double LfoWaveCost[8] = {0.0, 1.5, 2.2, -4.2, 7.0, 21.6, 5.5, 5.5};
		constexpr double LfoTriggerCost = 4.5;
		constexpr double LfoLagCost = 3.0;
		constexpr double PitchRouteCost = 67.0; // LFO1 -> DCO1 frequency
		constexpr double PwRouteCost = 45.0;    // LFO2 -> pulse width with pulse on
		constexpr double PwRouteOffCost = 14.0; // the same with pulse off
		// envelopes (env/ jobs, cycle length against the idle cycle of the same patch): extra time
		// for a voice by stage, the largest over its envelopes with amplitude; routes cost every block
		constexpr double EnvStageCost[3][6] = {
			// idle, delay, attack, decay, sustain, release
			{0, 37, 61, 88, 42, 46},
			{0, 37, 4, 67, 21, 46},
			{0, 37, 62, 89, 43, 46}};
		constexpr double VcfByEnv1Cost = 110.0;
		constexpr double EnvLfoTrigCost = 75.0;   // per envelope with an LFO trigger (env/ lfotrig)
		constexpr double EnvLfoTrigSharedCost = 6.0;   // the same with LFO1 already in use (env4/)
		constexpr double EnvVelocityCost = 37.0;  // per envelope with amp by velocity
		constexpr double BusCost = 34.0;          // per matrix bus in use (ramp/ jobs)
		constexpr double PortaVelocityCost = 58.0; // porta by velocity (porta3/vel_*)
		constexpr double BusPortaCost = 22.0;     // extra for a bus to portamento time (porta3/bus*)
		constexpr double BusLfoSourceCost = 28.0;   // matrix/audio_lfo1_vcf: cycle 12.86 ms, not 12.70
		constexpr double BusPitchDestCost = 77.0;   // envelope to DCO frequency (matrix/audio_env1_dco1: cycle 12.99 ms, not 12.52)
		constexpr double BusVibratoCost = 11.0;     // a vibrato bus instead of BusCost (matrix/audio_vibrato_dco1)
		constexpr double BusStageCost = 10.0;     // extra for a bus to an envelope or LFO parameter (matrix2: cycle 0.5 % short)
		constexpr double PortaLagCost = 8.0;      // lag mode other than 0 (porta2/long_*_l1, porta3/lag_once)
		constexpr double PortaCost = 49.5;        // per DCO1 (and VCF) following portamento, gliding or not (porta4/)
		constexpr double PortaDco2Cost = 14.5;    // DCO2 following portamento as well (ws2/ glides 1.6 % slow at 49.5)
		constexpr double FmByEnv3Cost = 92.0;
		constexpr double PassCostLate = 157.0; // the same extra after PassCostLateUs
		constexpr double PassCostLateFirst = 1150.0;  // timing/idle gives 995 (ab/costfit.py pass); 995 loses 4.4 dB in the audio null, kept until the 2 s step onset is modelled
		constexpr double PassCostLateUs = 2145000.0;
		constexpr double ByteArrivalCost = 39.5;        // per received MIDI byte, added wherever it arrives (hold1: +39.5 per note byte)
		constexpr double SysexByteArrivalCost = 110.0;
		constexpr double SysexByteCost = 73.4;  // per SysEx byte handled at the poll
		constexpr double NoteOnCost = 248.0;    // plus NoteVoiceCost per voice that gets the note (548 for one)
		constexpr double NoteVoiceCost = 300.0;
		constexpr double OtherMsgCost = 276.0;  // note off: per voice released (unison: six)
		constexpr double SysexMsgCost = 300.0;
		constexpr double GlobalBlockCost = 63900.0;   // opcode 03 master block (vib/: first vibrato block 64 ms later)
		constexpr double PostLoadEditCost = 10000.0;

		// restart: offsets from the DCO read
		constexpr double Dco1Mode4 = 268.5;
		constexpr double Dco2Mode4 = 283.0, Dco2Mode4Patch = 276.5, Dco2Quick = 30.5;
		constexpr double ModeToRange1 = 189.5, ModeToRange2 = 185.5, QuickToRange = 180.5;
		constexpr double Dco2QuickR = 120.0;     // counts left: restart at once with the target count
		constexpr double Dco2LongWaitR = 19594.0;
		constexpr double ClickLead = 31.5, ClickCount = 71.0, ClickToRange = 97.5;   // dco/note_windows.csv
		constexpr double Dco2Decide = 800.0;    // ticks: OUT predicted at the reload
		constexpr double WaitWrite = 31.5, WaitFirstPoll = 38.5, WaitPoll = 6.5, WaitToRange = 97.0;

		// patch load, from the poll that handled the 0D message
		constexpr double BurstAt[3] = {1680.0, 17173.5, 32282.0};
		constexpr double BurstRound = 22.5;
		constexpr double BurstVoice[VoiceCount] = {0, 2.5, 5.5, 8.5, 11.5, 14.5};
		constexpr int BurstRounds = 50;
		constexpr double LatchAt[6] = {10986.5, 26087.0, 41195.5, 51801.5, 62407.5, 73013.5};
		constexpr double PatchResume = 79157.0;
		constexpr double PatchPollAfterF7 = 3477.5;
		constexpr double LoadPitchRouteUs = 8400.0;     // LFO1 -> DCO frequency
		constexpr double LoadPwRouteUs = 8600.0;        // LFO2 -> pulse width (lfo/: 25.6 ms with DCO1 + two PW)
		constexpr double LoadVcfByEnv1Us = 16600.0;
		constexpr double LoadFmByEnv3Us = 16500.0;   // env/env3_*: first restart 16.5 ms after the init patch's
		constexpr double LoadEnvLfoTrigUs = 8100.0;
		constexpr double LoadEnvBusUs = 17500.0;
		constexpr double LoadRampBusUs = 2200.0;        // ramp/
		constexpr double LoadLfoBusUs = 9400.0;         // matrix/audio_lfo1_vcf
		constexpr double LoadVibratoBusUs = 5700.0;     // matrix/audio_vibrato_dco1
		constexpr double LoadOtherBusUs = 1500.0;       // lever, pressure, keyboard, velocity sources (ctrl/, matrix/ renders)
		constexpr double LoadAmpByRampUs = 500.0;
		// a 0D load after the first one resumes 10.3 ms sooner (prog2/0d_note_40ms)
		constexpr double PatchResumeLater = PatchResume - 10334.0;
		// program change load (prog/, prog2/): one VCA2 burst at the end, no input until resume
		constexpr double ProgBurstAt = 68735.0;
		constexpr double ProgResume = 75000.0;

		// the patch load after the capture rig's cold boot restarts counters whose phase the
		// boot left behind (timing/ and init/ logs, identical in every job): read to mode word,
		// first count, mode word to target count, read to range latch; mode word < 0 = OUT wait
		struct BootRestart { double mode4; uint32_t first; double toTarget; double range; };

		// counter state the capture rig's cold boot leaves after its last pre-load timer write
		// (init/boot_regs.csv and the pre-load timer writes of init/writes.csv replayed through the
		// 8254 model with the voice board's sync gating): mode, count in use, count register, OUT.
		struct BootCounter { uint8_t mode; uint32_t reload; uint32_t count; bool out; };
		constexpr double BootStateUs = 91500.0;   // after the last pre-load timer write; the boot sync stays on until the load writes the control latch
		constexpr uint8_t BootControlLatch = 37;
		constexpr uint64_t SyncPulseTicks = 7;     // as the voice board
		constexpr BootCounter BootCounters[VoiceCount * 2] = {
			{2, 10373, 8787, true}, {3, 3910, 722, false}, {2, 10254, 7772, true}, {3, 11784, 6804, false},
			{2, 10276, 10256, true}, {3, 115, 58, false}, {2, 10432, 9634, true}, {3, 831, 48, true},
			{2, 10342, 6126, true}, {3, 10297, 1848, false}, {2, 10394, 10342, true}, {3, 445, 324, false}};
		constexpr BootRestart BootTable[VoiceCount * 2] = {
			{242.5, 4546, 80.5, 421.5}, {-1.0, 0, 87.5, 183.5},
			{268.5, 8594, 91.0, 458.0}, {276.5, 3622, 89.5, 462.0},
			{255.5, 1491, 80.5, 434.5}, {30.5, 18182, 79.0, 205.5},
			{262.0, 14489, 92.0, 452.5}, {263.5, 2, 90.5, 450.0},
			{255.5, 11932, 81.5, 435.5}, {291.5, 16264, 80.0, 467.5},
			{268.5, 13352, 92.0, 459.0}, {30.5, 18182, 90.5, 217.0}};
	}

	// 8254 counter

	void Core::DcoPhase::roll(const int64_t _tick)
	{
		if(stopped)
			return;
		for(;;)
		{
			const int64_t len = mode == 3 ? std::max<int64_t>(1, n / 2) : std::max<int64_t>(1, n);
			if(_tick < start + len)
				return;
			if(pending < 0)
			{
				const int64_t k = (_tick - start) / len;
				start += k * len;
				if(mode == 3 && (k & 1)) high = !high;
				return;
			}
			start += len;
			if(mode == 3) high = !high;
			n = static_cast<uint32_t>(pending);
			pending = -1;
		}
	}

	uint32_t Core::DcoPhase::count(const int64_t _tick)
	{
		roll(_tick);
		if(stopped)
			return n;
		const int64_t c = mode == 3 ? n - 2 * (_tick - start) : n - (_tick - start);
		return static_cast<uint32_t>(std::max<int64_t>(c, 1));
	}

	int64_t Core::DcoPhase::nextRise(const int64_t _tick)
	{
		roll(_tick);
		if(stopped || mode != 3 || high)
			return _tick;
		return start + std::max<int64_t>(1, n / 2);
	}

	void Core::DcoPhase::writeCount(const int64_t _tick, const uint32_t _count)
	{
		if(stopped)
		{
			n = _count;
			start = _tick + 1;
			high = true;
			stopped = false;
			pending = -1;
			return;
		}
		roll(_tick);
		pending = _count;
	}

	// scheduler

	void Core::resetScheduler()
	{
		for(uint32_t i = 0; i < VoiceCount; ++i)
			m_order[i] = static_cast<uint8_t>(i);
		// the boot ends inside voice 5's block (first write 109 us after the job start)
		m_pos = 5;
		m_nextBlock = 109.0;
		m_nextSmooth = 0.0;
		m_cpu = 0.0;
		m_lastPoll = 0.0;
		m_lastGlobal = -1.0;
		m_passVoice = 5;
		m_passLate = false;
		m_patchLoaded = false;
		m_newAlloc.clear();
		m_byteCost.clear();
		m_byteCostSysex = false;
		m_sysexBytes = 0;
		m_lastF7 = -1.0;
		m_programLoadAt = -1.0;
		m_stalled = false;
		m_loadRestart = false;
		m_afterLoad = false;
		m_postLoadEditDone = false;
		m_coldBoot = true;
		for(auto& b : m_bootTableRestart) b[0] = b[1] = false;
		// the boot leaves the latches as the init patch sets them; no write until a patch load
		m_global.wave1 = m_edit.dco(0).wave;
		m_global.wave2 = m_edit.dco(1).wave;
		m_global.sync = m_edit.sync();
		m_lastCtrl = cvMap::controlLatch(m_global);
		m_lastNoise = cvMap::noiseLatch(m_global) ? 1 : 0;
		// the boot leaves DCO1 counters in mode 2 and DCO2 in mode 3 (init/boot_regs.csv); write them so
		// a board fed with the Core's stream alone has the modes too
		for(uint32_t v = 0; v < VoiceCount; ++v)
		{
			if(v == 0)
			{
				for(auto& pt : m_pit)
					pt.reset();
				m_mirrorBoot = true;
				m_mirrorCtrl = BootControlLatch;
				for(uint32_t i = 0; i < VoiceCount; ++i) { m_syncScanned[i] = static_cast<uint64_t>(ticks(BootStateUs)); m_syncLevel[i] = false; }
				for(uint32_t ch = 0; ch < VoiceCount * 2; ++ch)
				{
					const auto& b = BootCounters[ch];
					const uint32_t vv = ch / 2, d = ch % 2, chip = d * 2 + vv / 3, counter = vv % 3;
					const int64_t T = ticks(BootStateUs);
					const int64_t n = b.reload ? b.reload : 65536;
					const int64_t elapsed = b.mode == 3 ? (b.out ? (n - b.count) / 2 : n / 2 + (n - b.count) / 2) : n - b.count;
					const auto at = static_cast<uint64_t>(std::max<int64_t>(0, T - elapsed - 1));
					m_pit[chip].write(3, static_cast<uint8_t>((counter << 6) | 0x30 | (b.mode << 1)), at);
					m_pit[chip].write(static_cast<uint8_t>(counter), static_cast<uint8_t>(b.reload & 0xff), at);
					m_pit[chip].write(static_cast<uint8_t>(counter), static_cast<uint8_t>((b.reload >> 8) & 0xff), at);
				}
			}
			m_phase[v][0] = DcoPhase();
			m_phase[v][1] = DcoPhase();
			emitDcoMode(0.0, v, 0, 2);
			emitDcoCount(0.0, v, 0, m_phase[v][0].n);
			emitDcoMode(0.0, v, 1, 3);
			emitDcoCount(0.0, v, 1, m_phase[v][1].n);
			m_voices[v].lastCount[0] = m_voices[v].lastCount[1] = 0;
		}
	}

	void Core::queueByteCost(const double _time, const uint8_t _byte)
	{
		if(_byte == 0xf0) m_byteCostSysex = true;
		const bool sx = m_byteCostSysex;
		if(_byte >= 0x80 && _byte < 0xf8 && _byte != 0xf0) m_byteCostSysex = false;
		m_byteCost.push_back({_time, sx ? SysexByteArrivalCost : ByteArrivalCost});
	}

	double Core::work(const double _us)
	{
		double end = m_cpu + _us;
		while(!m_byteCost.empty() && m_byteCost.front().first < end)
		{
			if(m_byteCost.front().first >= m_cpu)
				end += m_byteCost.front().second;
			m_byteCost.pop_front();
		}
		m_cpu = end;
		return m_cpu;
	}

	void Core::skipTo(const double _time)
	{
		while(!m_byteCost.empty() && m_byteCost.front().first < _time)
			m_byteCost.pop_front();
		if(_time > m_cpu)
			m_cpu = _time;
	}

	void Core::sortWrites(const size_t _first)
	{
		// stable insertion sort by time: same order as std::stable_sort, without its buffer allocation
		for(size_t i = _first + 1; i < m_writes.size(); ++i)
		{
			const HwWrite w = m_writes[i];
			size_t j = i;
			for(; j > _first && w.timeUs < m_writes[j - 1].timeUs; --j)
				m_writes[j] = m_writes[j - 1];
			m_writes[j] = w;
		}
	}

	void Core::setOutputMode(const OutputMode _m)
	{
		if(_m == m_mode)
			return;
		m_mode = _m;
		if(_m == OutputMode::Smooth)
		{
			// start after the last Vintage block, write every value again
			m_nextSmooth = std::max({m_nextSmooth, m_now, m_cpu});
			m_byteCost.clear();
			for(auto& vs : m_voices)
			{
				std::fill(std::begin(vs.lastCv), std::end(vs.lastCv), 0u);
				vs.lastTickUs = -1.0;
			}
			std::fill(std::begin(m_lastRange), std::end(m_lastRange), -1);
			return;
		}
		// Vintage: the next block starts now, its first time step is one idle cycle
		m_nextBlock = std::max(m_nextBlock, m_now);
		m_cpu = std::max(m_cpu, m_now);
		m_byteCost.clear();
		m_lastGlobal = -1.0;
		for(auto& vs : m_voices)
			vs.lastTickUs = -1.0;
	}

	void Core::setSmoothStepUs(const double _us)
	{
		if(std::isfinite(_us) && _us >= 1.0)
			m_smoothStepUs = _us;
	}

	void Core::advance(const double _timeUs)
	{
		if(m_mode == OutputMode::Smooth)
		{
			while(m_nextSmooth <= _timeUs)
			{
				feedMidi(m_nextSmooth, true);
				smoothStep(m_nextSmooth, m_smoothStepUs);
				m_nextSmooth += m_smoothStepUs;
			}
			m_byteCost.clear();
			m_now = _timeUs;
			return;
		}

		while(m_nextBlock <= _timeUs)
		{
			skipTo(m_nextBlock);
			const size_t first = m_writes.size();
			const uint32_t v = m_order[m_pos];
			const bool note = (m_voices[v].restart & RestartNote) != 0;
			if(!(m_voices[v].restart & RestartPatch))
				m_afterLoad = false;
			runBlock();
			const double last = m_cpu;
			work(PollAfter);
			poll();
			const bool loaded = m_patchLoaded;
			if(loaded)
			{
				m_patchLoaded = false;
				patchLoad();
				// MIDI that arrived during the load is taken before the first block after it
				m_pos = (m_pos + VoiceCount - 1) % VoiceCount;
				m_afterLoad = !m_postLoadEditDone;
				m_postLoadEditDone = false;
				poll();
			}
			// mux inhibits and latch strobes are emitted out of order inside a block
			sortWrites(first);
			if(loaded)
			{
				m_nextBlock = m_cpu;
				continue;
			}
			const uint32_t next = m_order[m_pos];
			work((note ? GapAfterNote : GapAfter) - PollAfter);
			work(envelopeCost(next));
			if(m_voices[next].restart & RestartNote)
			{
				// the restart block right after the poll that took the note needs less (timing/latency_*)
				work(m_voices[next].prepShort ? NotePrepShortCost : NotePrepCost);
				m_voices[next].prepShort = false;
			}
			else if(next == m_passVoice)
			{
				// the per-pass extra time depends on the vibrato amplitude modulation: with the amp mod
				// source off it shrinks to 17 us and the 2 s step never comes (vib/ amod_src0 vs src1)
				if(m_master.vibWave() == 4)
					work(VibRandomCost);
				if(m_master.vibSpeedModSource() != 0 && m_master.vibSpeedModAmount() != 0)
					work(VibSpeedModCost);
				if(m_master.vibAmpModSource() == 0 || m_master.vibAmpModAmount() == 0)
					work(PassCostNoAmpMod);
				else
				{
					work(PassCost);
					if(last >= PassCostLateUs)
					{
						work(PassCostLate + (m_passLate ? 0.0 : PassCostLateFirst));
						m_passLate = true;
					}
				}
			}
			m_nextBlock = m_cpu;
		}
		m_now = _timeUs;
	}

	bool Core::feedMidi(const double _until, const bool _drain)
	{
		// returns true when a channel message was handled
		bool handled = false;
		while(!m_inQueue.empty() && m_inQueue.front().time <= _until)
		{
			const auto q = m_inQueue.front();
			m_inQueue.pop_front();
			if(q.byte == 0xf0 || m_sysexBytes) ++m_sysexBytes;
			if(q.byte == 0xf7) m_lastF7 = q.time;
			if(m_master.echo() && q.byte != 0xfe)
				m_midiOut.push_back(q.byte);
			auto& msg = m_msg;
			if(!m_midi.push(q.byte, msg))
				continue;
			if(msg.status == 0xf0)
			{
				const auto modeBefore = m_edit.keyboardMode();
				handleMessage(msg);
				if(m_mode == OutputMode::Vintage && !m_patchLoaded)
					work(msg.sysex.size() > 3 && msg.sysex[3] == 0x03 ? GlobalBlockCost : SysexMsgCost);
				// a keyboard mode change taken during or right after a patch load stalls 10 ms more
				// (sched4/ P48 2; porta4 with P48 0, no change, does not)
				if(m_mode == OutputMode::Vintage && (m_afterLoad || (m_patchLoaded && !m_postLoadEditDone)) && msg.sysex.size() > 4 && msg.sysex[3] == 0x06
					&& msg.sysex[4] == 48 && m_edit.keyboardMode() != modeBefore)
				{
					work(PostLoadEditCost);
					m_afterLoad = false;
					m_postLoadEditDone = true;
				}
				continue;
			}
			m_sysexBytes = 0;
			if(!_drain)
				continue;
			handled = true;
			handleMessage(msg);
			uint32_t ons = 0, offs = 0;
			for(const auto& e : m_allocEvents)
			{
				if(e.type == VoiceAllocator::Event::Type::Off) ++offs; else ++ons;
				// a released voice moves in the service list like a new note (ctrl/audio_sustain_release30)

				if(e.type != VoiceAllocator::Event::Type::On)
					continue;
				m_voices[e.voice].restart |= RestartNote;
				m_newAlloc.push_back(e.voice);
			}
			processAllocEvents();
			applyInserts();
			if(m_mode == OutputMode::Vintage)
				work((msg.status & 0xf0) == 0x90 && msg.data2 ? NoteOnCost + NoteVoiceCost * std::max(1u, ons) : OtherMsgCost * std::max(1u, offs));
		}
		return handled;
	}

	void Core::applyInserts()
	{
		// sched/ pair and chord jobs, timing/latency_*: a voice that gets a note at the poll after
		// the block of voice cur moves behind next(cur) and behind the run of voices waiting for
		// a note restart that follows it; if it already sits in that run it stays
		// voices of one message (unison) go in in voice order: the run does not include the
		// voices of the same message that are not placed yet
		std::sort(m_newAlloc.begin(), m_newAlloc.end());
		for(size_t k = 0; k < m_newAlloc.size(); ++k)
		{
			const uint32_t x = m_newAlloc[k];
			auto later = [&](const uint32_t _v) { return std::find(m_newAlloc.begin() + static_cast<std::ptrdiff_t>(k) + 1, m_newAlloc.end(), _v) != m_newAlloc.end(); };
			auto at = [&](const uint32_t _i) { return static_cast<uint32_t>(m_order[(m_pos + _i) % VoiceCount]); };
			if(at(1) == x)
			{
				m_voices[x].prepShort = true;
				continue;
			}
			uint32_t anchor = 1;
			bool stays = false;
			while(anchor + 1 < VoiceCount)
			{
				const uint32_t n = at(anchor + 1);
				if(n == x) { stays = true; break; }
				const bool placed = std::find(m_newAlloc.begin(), m_newAlloc.begin() + static_cast<std::ptrdiff_t>(k), n) != m_newAlloc.begin() + static_cast<std::ptrdiff_t>(k);
				if((!(m_voices[n].restart & RestartNote) && !placed) || later(n)) break;
				++anchor;
			}
			if(stays)
				continue;
			const uint32_t after = at(anchor), nxt = at(1);
			// fixed size: this runs on the audio thread
			std::array<uint8_t, VoiceCount> l{};
			uint32_t count = 0;
			for(uint32_t i = 0; i < VoiceCount; ++i)
				if(at(i) != x)
					l[count++] = static_cast<uint8_t>(at(i));
			const auto pos = std::min<uint32_t>(static_cast<uint32_t>(std::find(l.begin(), l.begin() + count, static_cast<uint8_t>(after)) - l.begin()), count - 1);
			for(uint32_t i = count; i > pos + 1; --i)
				l[i] = l[i - 1];
			l[pos + 1] = static_cast<uint8_t>(x);
			for(uint32_t i = 0; i < VoiceCount; ++i)
				m_order[i] = l[i];
			// keep next(cur) as the next block; the poll steps m_pos on
			const auto c = std::find(l.begin(), l.end(), static_cast<uint8_t>(nxt)) - l.begin();
			m_pos = static_cast<uint32_t>((c + VoiceCount - 1) % VoiceCount);
		}
		m_newAlloc.clear();
	}

	void Core::poll()
	{
		m_now = m_cpu;

		// bytes received so far; keep going while channel messages complete
		for(;;)
		{
			m_sysexBytes = m_sysexBytes ? 1 : 0;
			const bool more = feedMidi(m_cpu, true);
			if(m_sysexBytes > 1) work((m_sysexBytes - 1) * SysexByteCost);
			// a stalling message (edit, program change, RPN) lets the input that arrived meanwhile through
			const bool stalled = m_stalled;
			m_stalled = false;
			if(!more && !stalled) break;
		}


		m_global.wave1 = m_edit.dco(0).wave;
		m_global.wave2 = m_edit.dco(1).wave;
		m_global.sync = m_edit.sync();
		m_global.click[0] = m_edit.dco(0).click;
		m_global.click[1] = m_edit.dco(1).click;
		if(!m_patchLoaded && (cvMap::controlLatch(m_global) != m_lastCtrl || (cvMap::noiseLatch(m_global) ? 1 : 0) != m_lastNoise))
			emitLatches(m_cpu);

		m_pos = (m_pos + 1) % VoiceCount;
		m_lastPoll = m_cpu;
	}

	double Core::loadModulatorUs() const
	{
		// every modulation route the patch has adds to the load time (sched7/: one
		// change each against the init patch, additive in the combined patches)
		const auto& p = m_edit;
		double us = 0.0;
		const bool lfo1 = p.lfo(0).amp || p.lfo(0).ampByRamp, lfo2 = p.lfo(1).amp || p.lfo(1).ampByRamp;
		bool pitchRoute = false;
		for(uint32_t d = 0; d < 2; ++d)
		{
			if(lfo1 && p.dco(d).lfo1Freq) { us += LoadPitchRouteUs; pitchRoute = true; }
			if(lfo2 && p.dco(d).lfo2Pw) us += LoadPwRouteUs;
		}
		if(p.vcfByEnv1()) us += LoadVcfByEnv1Us;
		if(p.fmByEnv3()) us += LoadFmByEnv3Us;
		for(uint32_t i = 0; i < 3; ++i)
		{
			const auto e = p.env(i);
			// shared with the LFO1 pitch route when there is one (env4/)
			if(e.lfoTrigger && !pitchRoute)
				us += LoadEnvLfoTrigUs;
		}
		for(uint32_t b = 0; b < Patch::MatrixBuses; ++b)
		{
			const auto bus = p.bus(b);
			if(bus.source == Source::None || bus.dest == Dest::None || !bus.amount)
				continue;
			const bool env = bus.source == Source::Env1 || bus.source == Source::Env2 || bus.source == Source::Env3;
			const bool ramp = bus.source == Source::Ramp1 || bus.source == Source::Ramp2;
			const bool lfo = bus.source == Source::Lfo1 || bus.source == Source::Lfo2;
			us += env ? LoadEnvBusUs : (ramp ? LoadRampBusUs : (lfo ? LoadLfoBusUs : (bus.source == Source::Vibrato ? LoadVibratoBusUs : LoadOtherBusUs)));
		}
		for(uint32_t i = 0; i < 2; ++i)
			if(p.lfo(i).ampByRamp) us += LoadAmpByRampUs;
		return us;
	}

	void Core::patchLoad()
	{
		// anchored to the end of the message: the block running around it does not move the sequence
		const bool prog = m_programLoadAt >= 0.0;
		const double p = prog ? m_programLoadAt : (m_lastF7 >= 0.0 && m_lastF7 + PatchPollAfterF7 > m_cpu - 5000.0 ? m_lastF7 + PatchPollAfterF7 : m_cpu);
		m_programLoadAt = -1.0;
		const size_t first = m_writes.size();
		// the extra load time stretches the whole sequence measured from the message end (sched7/:
		// every latch write moves by the same ratio as the first restart)
		const double base = (m_coldBoot ? PatchResume : PatchResumeLater) + PatchPollAfterF7;
		const double stretch = prog ? 1.0 : (base + loadModulatorUs()) / base;
		auto stretched = [&](const double _ofs) { return (_ofs + PatchPollAfterF7) * stretch - PatchPollAfterF7; };
		for(const auto b0 : BurstAt)
		{
			const double b = stretched(b0);
			if(!prog || b0 == BurstAt[0])
			for(int r = 0; r < BurstRounds; ++r)
				for(uint32_t v = 0; v < VoiceCount; ++v)
					emitCv(p + (prog ? ProgBurstAt : b) + r * BurstRound + BurstVoice[v], voiceCv(v, CvLin), 0, false, 0.0);
		}
		const double resume = prog ? ProgResume : (m_coldBoot ? PatchResume : PatchResumeLater) + loadModulatorUs();
		for(const auto l0 : LatchAt)
			if(stretched(l0) < resume)
				emitLatches(p + stretched(l0));
		sortWrites(first);
		for(uint32_t v = 0; v < VoiceCount; ++v)
		{
			m_voices[v].restart |= RestartPatch;
			m_bootTableRestart[v][0] = m_bootTableRestart[v][1] = m_coldBoot;
		}
		m_coldBoot = false;
		m_mirrorBoot = false;
		m_cpu = p;
		skipTo(p + resume);
		// a program change is taken one block later than other messages: the voice after next
		// gets the first restart (prog/, prog2/: voice 4 where a note at the same time picks 3)
		if(prog)
			m_pos = (m_pos + 1) % VoiceCount;
	}

	void Core::scanSync(const uint64_t _until)
	{
		// the voice board's SYNC1: a DCO2 falling edge holds the DCO1 counter's GATE low
		const bool gate = !(m_mirrorCtrl & CtlSync1Off);
		for(uint32_t v = 0; v < VoiceCount; ++v)
		{
			auto& a = m_pit[v / 3];
			auto& b = m_pit[2 + v / 3];
			const uint32_t ch = v % 3;
			uint64_t t = m_syncScanned[v];
			if(_until <= t)
				continue;
			for(;;)
			{
				const bool level = b.getOut(ch, t);
				if(level != m_syncLevel[v])
				{
					m_syncLevel[v] = level;
					if(!level && gate)
						a.gate(ch, t, t + SyncPulseTicks);
				}
				const uint64_t e = b.nextOutEdge(ch, t);
				if(e >= _until)
					break;
				t = e;
			}
			m_syncScanned[v] = _until;
		}
	}

	void Core::emitLatches(const double _time)
	{
		const auto ctrl = cvMap::controlLatch(m_global);
		const int noise = cvMap::noiseLatch(m_global) ? 1 : 0;
		emit(_time, Target::Latch, cvMap::AuxLatch, cvMap::AuxLatchValue);
		emit(_time + 113.0, Target::Ctrl, 0, ctrl);
		scanSync(static_cast<uint64_t>(ticks(_time + 113.0)));
		m_mirrorCtrl = ctrl;
		emit(_time + 120.0, Target::Noise, 0, static_cast<uint32_t>(noise));
		m_lastCtrl = ctrl;
		m_lastNoise = noise;
	}

	uint32_t Core::cvCode(const uint32_t _v, const CvIndex _i, const VoiceControls& _c) const
	{
		if(m_analog && m_analog->enabled())
			return m_analog->cvValue(_v, _i, _c);
		return cvMap::cvValue(_v, _i, _c);
	}

	void Core::emitCv(const double _time, const uint32_t _channel, const uint32_t _code, const bool _fast, const double _muxLead)
	{
		const uint32_t dac = _code & 0xfff;
		if(_muxLead > 0.0)
			emit(_time - _muxLead, Target::Mux, 0, 1);
		emit(_time, Target::Cv, static_cast<uint16_t>(_channel), dac | (_fast ? 0u : CvSlow) | (static_cast<uint32_t>(cvMap::dacRange(_channel, dac)) << 13));
	}

	void Core::emitDcoMode(const double _time, const uint32_t _v, const uint32_t _d, const uint8_t _mode)
	{
		m_phase[_v][_d].writeMode(_mode);
		const uint32_t chip = _d * 2 + _v / 3, counter = _v % 3;
		scanSync(static_cast<uint64_t>(ticks(_time)));
		if(!m_mirrorBoot) m_pit[chip].write(3, static_cast<uint8_t>((counter << 6) | 0x30 | ((_mode & 7) << 1)), static_cast<uint64_t>(ticks(_time)));
		emit(_time, Target::DcoMode, static_cast<uint16_t>(_v * 2 + _d), _mode);
	}

	void Core::emitDcoCount(const double _time, const uint32_t _v, const uint32_t _d, const uint32_t _count)
	{
		m_phase[_v][_d].writeCount(ticks(_time), _count);
		scanSync(static_cast<uint64_t>(ticks(_time)));
		if(!m_mirrorBoot)
		{
			const uint32_t chip = _d * 2 + _v / 3, counter = _v % 3;
			const auto tk = static_cast<uint64_t>(ticks(_time));
			m_pit[chip].write(static_cast<uint8_t>(counter), static_cast<uint8_t>(_count & 0xff), tk);
			m_pit[chip].write(static_cast<uint8_t>(counter), static_cast<uint8_t>((_count >> 8) & 0xff), tk);
		}
		m_voices[_v].lastCount[_d] = static_cast<uint16_t>(_count);
		emit(_time, Target::Dco, static_cast<uint16_t>(_v * 2 + _d), _count);
	}

	void Core::restartDco(const uint32_t _v, const uint32_t _d, const bool _note)
	{
		// m_cpu is at the counter read; leaves m_cpu at the range latch write
		auto& ph = m_phase[_v][_d];
		const auto& tm = m_voices[_v].timer[_d];
		const double read = m_cpu;
		const int64_t tr = ticks(read);
		const uint32_t chip = _d * 2 + _v / 3, counter = _v % 3;
		auto& pit = m_pit[chip];
		scanSync(static_cast<uint64_t>(tr));
		const uint32_t reload = pit.getCounter(counter).reload;
		const double n = reload ? reload : 65536.0;
		const double r = pit.getCount(counter, static_cast<uint64_t>(tr));
		(void)ph;
		const double target = tm.count ? tm.count : 65536.0;
		const uint8_t mode = _d == 0 ? 2 : 3;

		auto sequence = [&](const double _mode4, const uint32_t _first, const double _toTarget, const double _range)
		{
			emitDcoMode(work(_mode4), _v, _d, 4);
			emitDcoCount(work(5.0), _v, _d, 65535);
			emitDcoMode(work(_d == 0 ? 3.0 : 4.0), _v, _d, mode);
			emitDcoCount(work(7.5), _v, _d, _first);
			emitDcoCount(work(_toTarget - 15.5 - (_d == 0 ? 0.0 : 1.0)), _v, _d, tm.count);
			work(read + _range - m_cpu);
		};

		// click (P09/P19): mode 3 with count 1, the note count follows at the next refresh (dco/note_windows.csv)
		if(_note && m_edit.dco(_d).click)
		{
			const double t = read - ClickLead;
			emitDcoMode(t, _v, _d, 3);
			emitDcoCount(t + ClickCount, _v, _d, 1);
			m_cpu = std::max(m_cpu, t + ClickCount + ClickToRange);
			return;
		}


		// keep the converter's phase: the new first count is the scaled time to the next edge
		auto firstCount = [&](double _x, const double _period)
		{
			while(_x <= 0.0) _x += _period;
			return static_cast<uint32_t>(std::clamp(std::lround(_x * target / n), 2L, 65535L));
		};

		// [R-DCO] first count from the read back phase, 8 bit (cvDco.cpp); 0 = no phase count
		const auto phase = [&]() { return dco::phaseCount(_d, static_cast<uint16_t>(std::min(n, 65535.0)), static_cast<uint16_t>(r), tm.count); };
		(void)firstCount;

		// the first load after the rig's cold boot: the measured sequence timing per counter, the
		// first count from the boot state read back (BootCounters)
		if(m_bootTableRestart[_v][_d])
		{
			m_bootTableRestart[_v][_d] = false;
			const auto& b = BootTable[_v * 2 + _d];
			if(b.mode4 >= 0.0)
			{
				const uint16_t x = b.mode4 < 100.0 ? 0 : phase();
				sequence(b.mode4, x ? x : tm.count, b.toTarget, b.range);
			}
			else
			{
				emitDcoCount(work(b.toTarget), _v, _d, tm.count);
				work(read + b.range - m_cpu);
			}
			return;
		}

		if(_d == 0)
		{
			const uint16_t x = phase();
			sequence(Dco1Mode4, x ? x : tm.count, 75.5 + 15.5, Dco1Mode4 + ModeToRange1);
			return;
		}

		const bool high = pit.getOut(counter, static_cast<uint64_t>(tr));
		// OUT low with a long way to go restarts at once (sched6/: waits up to R 19154, restarts
		// at once from R 20034; cut halfway)
		if(r < Dco2QuickR || (!high && r > Dco2LongWaitR))
		{
			sequence(Dco2Quick, tm.count, 73.0 + 16.5, Dco2Quick + QuickToRange);
			return;
		}
		if(high ? (r / 2.0 > Dco2Decide) : (r / 2.0 < Dco2Decide))
		{
			const double m4 = _note ? Dco2Mode4 : Dco2Mode4Patch;
			const uint16_t x = phase();
			sequence(m4, x ? x : tm.count, 73.0 + 16.5, m4 + ModeToRange2);
			return;
		}

		// OUT low at the reload: a patch load restart writes the count only (init: voice 0 DCO2 after
		// the boot load); a note on or a count step writes it and polls the status until OUT goes
		// high (timing/bend_steps)
		if(!_note && m_loadRestart)
		{
			emitDcoCount(work(87.5), _v, 1, tm.count);
			work(96.0);
			return;
		}
		emitDcoCount(work(WaitWrite), _v, 1, tm.count);
		const double p0 = work(WaitFirstPoll - WaitWrite);
		double rise = p0;
		{
			auto tk = static_cast<uint64_t>(ticks(p0));
			for(int guard = 0; guard < 4 && !pit.getOut(counter, tk); ++guard)
			{
				const uint64_t e = pit.nextOutEdge(counter, tk);
				if(e == ~0ull) break;
				tk = e;
			}
			rise = tk / 2.0;
		}
		skipTo(rise <= p0 ? p0 : p0 + std::ceil((rise - p0) / WaitPoll) * WaitPoll);
		work(WaitToRange);
	}

	bool Core::busUses(const Source _s) const
	{
		for(uint32_t b = 0; b < Patch::MatrixBuses; ++b)
		{
			const auto bus = m_edit.bus(b);
			if(bus.source == _s && bus.dest != Dest::None && bus.amount)
				return true;
		}
		return false;
	}

	double Core::modulatorCost(const uint32_t) const
	{
		const auto& p = m_edit;
		double cost = 0.0;
		for(uint32_t i = 0; i < 2; ++i)
		{
			const auto l = p.lfo(i);
			// an LFO runs when it has amplitude, amp by ramp or feeds a matrix bus (matrix/audio_lfo1_vcf)
			if(l.amp == 0 && l.ampByRamp == 0 && !busUses(i == 0 ? Source::Lfo1 : Source::Lfo2))
				continue;
			cost += LfoCost + LfoWaveCost[static_cast<size_t>(l.wave) & 7];
			if(l.trigger != LfoTrigger::None) cost += LfoTriggerCost;
			if(l.lag) cost += LfoLagCost;
			if(l.speedMod)
				cost += LfoSpeedModCost;
			if(l.ampByRamp)
				cost += LfoRampCost;
		}
		if(p.vcfByEnv1()) cost += VcfByEnv1Cost;

		for(uint32_t d = 0; d < 2; ++d)
			if(p.dco(d).portaKeyb & 1) cost += d == 0 ? PortaCost : PortaDco2Cost;
		if(p.vcfKeyb() & 1) cost += PortaCost;
		if(p.portaByVelocity()) cost += PortaVelocityCost;
		if(p.lagMode() != LagMode::ConstSpeed && ((p.dco(0).portaKeyb & 1) || (p.dco(1).portaKeyb & 1) || (p.vcfKeyb() & 1)))
			cost += PortaLagCost;
		if(p.fmByEnv3()) cost += FmByEnv3Cost;
		const bool lfo1 = p.lfo(0).amp || p.lfo(0).ampByRamp, lfo2 = p.lfo(1).amp || p.lfo(1).ampByRamp;
		if(lfo1 && p.dco(0).lfo1Freq) cost += PitchRouteCost;
		for(uint32_t i = 0; i < 3; ++i)
		{
			// LFO triggered envelopes need LFO1 running; that is most of the cost, already paid when
			// LFO1 is in use (env4/ against env2/lfotrig)
			if(p.env(i).lfoTrigger) cost += lfo1 ? EnvLfoTrigSharedCost : EnvLfoTrigCost;
			if(p.env(i).ampByVelocity) cost += EnvVelocityCost;
		}
		for(uint32_t b = 0; b < Patch::MatrixBuses; ++b)
			if(p.bus(b).source != Source::None && p.bus(b).dest != Dest::None && p.bus(b).amount != 0)   // an amount 0 bus costs nothing (matrix2/ lfo1_speed a0)
			{
				const auto bs = p.bus(b).source;
				const bool envSrc = bs == Source::Env1 || bs == Source::Env2 || bs == Source::Env3;
				cost += (bs == Source::Vibrato ? BusVibratoCost : BusCost) + (p.bus(b).dest == Dest::PortaTime ? BusPortaCost : 0.0)
					+ (bs == Source::Lfo1 || bs == Source::Lfo2 ? BusLfoSourceCost : 0.0)
					+ (envSrc && (p.bus(b).dest == Dest::Dco1Freq || p.bus(b).dest == Dest::Dco2Freq) ? BusPitchDestCost : 0.0)
					+ (p.bus(b).dest >= Dest::Env1Delay && p.bus(b).dest <= Dest::Lfo2Amp ? BusStageCost : 0.0);
			}
		for(uint32_t d = 0; d < 2; ++d)
			if(lfo2 && p.dco(d).lfo2Pw)
				cost += (p.dco(d).wave & 1) ? PwRouteCost : PwRouteOffCost;
		return cost;
	}

	double Core::envelopeCost(const uint32_t _v) const
	{
		double cost = 0.0;
		for(uint32_t i = 0; i < 3; ++i)
		{
			if(!m_edit.env(i).amp && !busUses(static_cast<Source>(static_cast<int>(Source::Env1) + i)))
				continue;
			const auto st = static_cast<size_t>(m_voices[_v].env[i].stage());
			cost = std::max(cost, EnvStageCost[i][st < 6 ? st : 0]);
		}
		return cost;
	}

	void Core::runBlock()
	{
		const double start = m_cpu;
		const uint32_t v = m_order[m_pos];
		auto& vs = m_voices[v];

		if(v == m_passVoice || m_lastGlobal < 0.0)
		{
			tickGlobal(m_lastGlobal < 0.0 ? RefreshUs : start - m_lastGlobal);
			m_lastGlobal = start;
		}
		tickVoice(v, vs.lastTickUs < 0.0 ? RefreshUs : start - vs.lastTickUs);
		vs.lastTickUs = start;

		work(modulatorCost(v));

		const auto& c = vs.controls;
		auto cv = [&](const CvIndex _i) { return cvCode(v, _i, c); };
		auto range = [&](const uint32_t _d)
		{
			const uint16_t ch = static_cast<uint16_t>(v * 2 + _d);
			const int r = vs.timer[_d].rangeHigh ? 1 : 0;
			const bool changed = m_lastRange[ch] >= 0 && m_lastRange[ch] != r;
			m_lastRange[ch] = r;
			emit(m_cpu, Target::Range, ch, static_cast<uint32_t>(r));
			return changed;
		};

		// a count step larger than 1/16 of the old count restarts that counter (lfo/, timing/bend_steps)
		bool jump[2] = {true, true};
		uint8_t restart = vs.restart;
		vs.restart = 0;
		m_loadRestart = (restart & RestartPatch) != 0;
		// a note that leaves both counts where they are (the same note again on its voice) does not
		// restart the counters (voice/m0_repeat8, m1_repeat8, env2/delay40)
		if(restart == RestartNote && vs.timer[0].count == vs.lastCount[0] && vs.timer[1].count == vs.lastCount[1])
			restart = 0;
		if(!restart)
		{
			for(uint32_t d = 0; d < 2; ++d)
			{
				const int32_t o = vs.lastCount[d], n = vs.timer[d].count;
				// portamento steps restart too when larger than 1/16 (porta/ rate 30-45 lag 0)
				jump[d] = o > 1 && std::abs(n - o) > (o >> 4);
			}
			if(jump[0] || jump[1])
				restart = RestartPatch;
		}
		else if(restart & RestartNote)
		{
			// a DCO following portamento is not restarted by the note, only by a large step (porta3/lag_once)
			for(uint32_t d = 0; d < 2; ++d)
			{
				const int32_t o = vs.lastCount[d], n = vs.timer[d].count;
				if(m_edit.dco(d).portaKeyb & 1)
					jump[d] = o > 1 && std::abs(n - o) > (o >> 4);
			}
		}
		const double s1 = Shift1[v], s2 = Shift2[v];

		emitCv(m_cpu, voiceCv(v, CvLog), cv(CvLog), false);

		if(!restart)
		{
			double nominal = 0.0;
			const auto at = [&](const Step _s)
			{
				const double o = BlockOffsets[_s] + (_s >= SDco2 ? s2 : (_s >= SDco1 ? s1 : 0.0));
				work(o - nominal);
				nominal = o;
				return m_cpu;
			};
			emitCv(at(SVca2a), voiceCv(v, CvLin), cv(CvLin), false);
			emitDcoCount(at(SDco1), v, 0, vs.timer[0].count);
			at(SRange1); range(0);
			emitCv(at(SWs1), voiceCv(v, CvWsA), cv(CvWsA), false, MuxLeadWs);
			emitCv(at(SPw1), voiceCv(v, CvPwA), cv(CvPwA), false);
			emitDcoCount(at(SDco2), v, 1, vs.timer[1].count);
			at(SRange2); range(1);
			emitCv(at(SWs2), voiceCv(v, CvWsB), cv(CvWsB), false, MuxLeadWs);
			emitCv(at(SPw2), voiceCv(v, CvPwB), cv(CvPwB), false);
			emitCv(at(SVcf), voiceCv(v, CvFreq), cv(CvFreq), (cv(CvFreq) & CvSlow) == 0);   // VCF porta: fast (vcf/ kbd)
			emitCv(at(SFm), voiceCv(v, CvMod), cv(CvMod), false);
			emitCv(at(SRes), resonanceCv(v), cvMap::resonanceValue(v, c), false);
			emitCv(at(SBal), balanceCv(v), cvMap::balanceValue(v, c), false);
			emitCv(at(SVca2b), voiceCv(v, CvLin), cv(CvLin), false);
			return;
		}

		// restart block (note on, or every voice after a patch load)
		const bool note = (restart & RestartNote) != 0;
		if(note)
			work(340.0);
		else
		{
			emitCv(work(BlockOffsets[SVca2a]), voiceCv(v, CvLin), cv(CvLin), false);
			work(378.0 - BlockOffsets[SVca2a]);
		}
		auto dco = [&](const uint32_t _d)
		{
			if(jump[_d])
				restartDco(v, _d, note);
			else
			{
				emitDcoCount(work(WaitWrite), v, _d, vs.timer[_d].count);
				work(WaitToRange);
			}
		};
		dco(0);
		bool rc = range(0);
		emitCv(work(2.0), voiceCv(v, CvWsA), cv(CvWsA), note || rc, MuxLeadWs);
		emitCv(work(92.5), voiceCv(v, CvPwA), cv(CvPwA), false);
		work(note ? 227.0 : 235.0);
		dco(1);
		const double r2 = m_cpu;
		rc = range(1);
		const auto tail = [&](const double _o) { return work(r2 + _o - m_cpu); };
		emitCv(tail(2.0), voiceCv(v, CvWsB), cv(CvWsB), note || rc, MuxLeadWs);
		emitCv(tail(94.5), voiceCv(v, CvPwB), cv(CvPwB), false);
		emitCv(tail(229.0), voiceCv(v, CvFreq), cv(CvFreq), (cv(CvFreq) & CvSlow) == 0);   // VCF porta: fast (vcf/ kbd)
		emitCv(tail(293.0), voiceCv(v, CvMod), cv(CvMod), false);
		emitCv(tail(336.5), resonanceCv(v), cvMap::resonanceValue(v, c), false);
		emitCv(tail(379.5), balanceCv(v), cvMap::balanceValue(v, c), false);
		emitCv(tail(418.5), voiceCv(v, CvLin), cv(CvLin), note);
		if(note)
			m_passVoice = v;
	}

	void Core::smoothStep(const double _time, const double _dtUs)
	{
		m_now = _time;
		m_newAlloc.clear();
		m_global.wave1 = m_edit.dco(0).wave;
		m_global.wave2 = m_edit.dco(1).wave;
		m_global.sync = m_edit.sync();
		m_global.click[0] = m_edit.dco(0).click;
		m_global.click[1] = m_edit.dco(1).click;
		if(m_patchLoaded || cvMap::controlLatch(m_global) != m_lastCtrl || (cvMap::noiseLatch(m_global) ? 1 : 0) != m_lastNoise)
		{
			m_patchLoaded = false;
			emitLatches(_time - 120.0 > 0.0 ? _time - 120.0 : 0.0);
		}

		// keep the DCO mirror's sync scan current (only the Vintage restart reads it), so a switch
		// to Vintage or a count write after a long idle does not scan the whole gap at once
		// (a step longer than 100 ms skips to the last 10 ms: Smooth does not read the mirror)
		const auto tk = static_cast<uint64_t>(ticks(_time));
		if(tk > m_syncScanned[0] + 20000)
		{
			if(tk > m_syncScanned[0] + 200000)
				for(auto& s : m_syncScanned)
					s = std::max(s, tk - 20000);
			scanSync(tk);
		}

		tickGlobal(_dtUs);
		for(uint32_t v = 0; v < VoiceCount; ++v)
		{
			auto& vs = m_voices[v];
			tickVoice(v, _dtUs);
			const auto& c = vs.controls;
			for(uint32_t d = 0; d < 2; ++d)
			{
				const uint16_t ch = static_cast<uint16_t>(v * 2 + d);
				const int r = vs.timer[d].rangeHigh ? 1 : 0;
				if(m_lastRange[ch] != r)
				{
					m_lastRange[ch] = r;
					emit(_time, Target::Range, ch, static_cast<uint32_t>(r));
				}
				if(vs.restart & RestartNote)
				{
					emitDcoMode(_time, v, d, d == 0 ? 2 : 3);
					emitDcoCount(_time, v, d, vs.timer[d].count);
				}
				else if(vs.timer[d].count != vs.lastCount[d])
					emitDcoCount(_time, v, d, vs.timer[d].count);
			}
			vs.restart = 0;
			for(uint32_t i = 0; i < 10; ++i)
			{
				const uint32_t ch = i < 8 ? voiceCv(v, static_cast<CvIndex>(i)) : (i == 8 ? balanceCv(v) : resonanceCv(v));
				const uint32_t code = (i < 8 ? cvCode(v, static_cast<CvIndex>(i), c) : (i == 8 ? cvMap::balanceValue(v, c) : cvMap::resonanceValue(v, c))) & 0xfff;
				if(vs.lastCv[i] == code + 1)
					continue;
				vs.lastCv[i] = code + 1;
				emitCv(_time, ch, code, true, 0.0);
			}
		}
	}
}
