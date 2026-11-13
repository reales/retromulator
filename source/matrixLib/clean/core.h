#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "cvMap.h"
#include "pit8254.h"
#include "master.h"
#include "matrix.h"
#include "midiIn.h"
#include "modulators.h"
#include "patch.h"
#include "porta.h"
#include "types.h"
#include "voiceAlloc.h"

namespace matrixClean
{
	namespace analog { class Unit; }

	// Matrix-1000 controller without firmware. Input: timestamped MIDI bytes. Output: a stream of
	// voice hardware writes (INTERFACE.md section 2) and MIDI out bytes.
	class Core
	{
	public:
		static constexpr uint32_t PatchCount = 1000;
		static constexpr uint32_t RamPatches = 200;
		static constexpr double ByteUs = 320.0;

		// idle refresh cycle (timing/cycles.csv); used as the first time step of a voice
		static constexpr double RefreshUs = 12231.0;

		Core();

		// cold boot: master re-init, init patches, initial hardware writes at time 0
		void reset();

		// one byte arriving at _timeUs; times must not decrease
		void midiIn(double _timeUs, uint8_t _byte);
		// a whole message, bytes 320 us apart from _timeUs; returns the time after the last byte
		double midiIn(double _timeUs, const std::vector<uint8_t>& _bytes);

		// runs the controller up to _timeUs
		void advance(double _timeUs);

		std::vector<HwWrite> takeWrites();
		std::vector<uint8_t> takeMidiOut();
		// into the caller's buffer, which is cleared first; both keep their capacity
		void takeWrites(std::vector<HwWrite>& _dst) { _dst.clear(); _dst.swap(m_writes); }
		void takeMidiOut(std::vector<uint8_t>& _dst) { _dst.clear(); _dst.swap(m_midiOut); }

		const Patch& editBuffer() const { return m_edit; }
		const Envelope& envelope(uint32_t _voice, uint32_t _i) const { return m_voices[_voice].env[_i]; }
		const Ramp& ramp(uint32_t _voice, uint32_t _i) const { return m_voices[_voice].ramp[_i]; }
		const Lfo& lfo(uint32_t _voice, uint32_t _i) const { return m_voices[_voice].lfo[_i]; }
		Patch& editBuffer() { return m_edit; }
		Master& master() { return m_master; }
		const Patch& storedPatch(uint32_t _number) const { return m_patches[_number % PatchCount]; }
		uint32_t currentPatch() const { return m_current; }
		double now() const { return m_now; }

		void loadEditBuffer(const Patch& _p);

		// persistent memory (patches, master, bank, patch number, edit buffer) as a versioned blob
		std::vector<uint8_t> getState() const;
		static size_t stateSize();   // bytes of getState()
		void getState(std::vector<uint8_t>& _dst) const;   // _dst is replaced, its capacity reused
		bool setState(const std::vector<uint8_t>& _state);

		// LFO 1/2 base speed 0-63 in place of the edit buffer's, -1 = the patch's (host tempo sync).
		// Never written into patch data, so stores, dumps and the state keep the patch speed
		void setLfoSpeedOverride(uint32_t _lfo, int _speed) { m_lfoSpeedOverride[_lfo & 1] = _speed; }
		int lfoSpeedOverride(uint32_t _lfo) const { return m_lfoSpeedOverride[_lfo & 1]; }

		// controller busy times (measured): program change load, RPN data entry
		static constexpr double ProgramLoadUs = 0.0;      // extra delay before the load sequence
		static constexpr double StoreRecallUs = 3300.0;
		static constexpr double RpnBusyUs = 64000.0;

		// a change at run time continues from now(): no catch up, no writes stamped in the past
		void setOutputMode(OutputMode _m);
		OutputMode outputMode() const { return m_mode; }
		// Smooth step; non-finite or below 1 us is ignored
		void setSmoothStepUs(double _us);
		const VoiceControls& controls(uint32_t _v) const { return m_voices[_v].controls; }

		// analog spread calibration (analog.h); null or a disabled unit writes the measured codes
		void setAnalog(const analog::Unit* _unit) { m_analog = _unit; }
		const analog::Unit* getAnalog() const { return m_analog; }

	private:
		// FIFO on a ring buffer: allocates only to grow beyond the reserved size
		template<typename T> class Fifo
		{
		public:
			explicit Fifo(const size_t _reserve) : m_buf(_reserve) {}
			bool empty() const { return m_size == 0; }
			const T& front() const { return m_buf[m_head]; }
			void pop_front() { m_head = (m_head + 1) % m_buf.size(); --m_size; }
			void clear() { m_head = m_size = 0; }
			void push_back(const T& _t)
			{
				if(m_size == m_buf.size())
				{
					std::vector<T> b(m_buf.size() * 2);
					for(size_t i = 0; i < m_size; ++i)
						b[i] = m_buf[(m_head + i) % m_buf.size()];
					m_buf.swap(b);
					m_head = 0;
				}
				m_buf[(m_head + m_size++) % m_buf.size()] = _t;
			}
		private:
			std::vector<T> m_buf;
			size_t m_head = 0, m_size = 0;
		};

		struct VoiceState
		{
			std::array<Envelope, 3> env;
			std::array<Lfo, 2> lfo;
			std::array<Ramp, 2> ramp;
			int32_t lfoRamp[2] = {65535, 65535};   // ramp level at the end of the previous block (LFO amp by ramp)
			Portamento porta;
			uint8_t note = 57;   // [R-PITCH]: idle voices hold count 18182 (key 57)
			uint8_t velocity = 0;
			uint8_t releaseVelocity = 0;
			bool gate = false;
			bool keyTracked = false;
			bool lfo1Rising = true;
			std::array<Mod, static_cast<size_t>(Source::Count)> src{};
			std::array<Mod, static_cast<size_t>(Source::Count)> busSrc{};   // sources the buses read
			std::array<Mod, static_cast<size_t>(Dest::Count)> dest{};
			VoiceControls controls;
			DcoTimer timer[2];
			uint16_t lastCount[2] = {};
			double lastTickUs = -1.0;
			uint8_t restart = 0;           // RestartNote / RestartPatch, done in the voice's next block
			bool prepShort = false;        // its note came at the poll right before its block
			uint32_t lastCv[10] = {};      // smooth mode: last code written (0 = never)
		};

		// 8254 counter state, enough to read back the count and OUT the way the restart does
		struct DcoPhase
		{
			uint8_t mode = 3;
			uint32_t n = 18182;
			int64_t pending = -1;
			int64_t start = 0;
			bool high = true;
			bool stopped = false;

			void roll(int64_t _tick);
			uint32_t count(int64_t _tick);
			bool out(int64_t _tick) { roll(_tick); return mode != 3 || high; }
			int64_t nextRise(int64_t _tick);
			void writeMode(uint8_t _mode) { mode = _mode; stopped = true; pending = -1; }
			void writeCount(int64_t _tick, uint32_t _count);
		};
		static constexpr uint8_t RestartNote = 1;
		static constexpr uint8_t RestartPatch = 2;

		struct QueuedByte { double time; uint8_t byte; };

		void handleMessage(const MidiMessage& _m);
		void handleSysex(const std::vector<uint8_t>& _s);
		void handleController(uint8_t _cc, uint8_t _value, int _voice);
		void handleProgram(uint8_t _program);
		void applyRegisteredParam(int _number, int _value);
		void dataEntry(int _value, bool _relative);
		bool channelMatches(uint8_t _channel, int& _voice) const;
		void selectPatch(uint32_t _number);
		void updateVolume();
		void scheduleSelect(uint32_t _number, double _delayUs);
		void stall(double _us);
		static double editCostUs(uint8_t _param, bool _portaOn, bool _unison);
		static constexpr double PortaOnEditUs = 3600.0;
		void cutVoices();
		void cutSoundingVoices();
		void storePatch(uint32_t _number, const Patch& _p);
		void sendPatch(const Patch& _p, uint8_t _bank, uint8_t _number);
		void sendBankDump();
		void sendMaster();

		void processAllocEvents();
		void noteOnVoice(uint32_t _v, uint8_t _note, uint8_t _velocity, bool _legato, bool _retarget, bool _voiceGated);
		void noteOffVoice(uint32_t _v, uint8_t _velocity);
		void externalTrigger(bool _down);
		void applyEnvExternal();
		void tickModulation(double _dtUs);
		void tickGlobal(double _dtUs);
		void vibratoStep(double _blocks);
		void latchVibrato();
		void tickVoice(uint32_t _v, double _dtUs);
		void computeSources(uint32_t _v, double _dtUs);
		void computeControls(uint32_t _v);
		// scheduler (refresh.cpp)
		void resetScheduler();
		bool feedMidi(double _until, bool _drain);
		void poll();
		void runBlock();
		double modulatorCost(uint32_t _v) const;
		bool busUses(Source _s) const;
		double envelopeCost(uint32_t _v) const;
		void restartDco(uint32_t _v, uint32_t _d, bool _note);
		double work(double _us);
		void skipTo(double _time);
		void queueByteCost(double _time, uint8_t _byte);
		void emitCv(double _time, uint32_t _channel, uint32_t _code, bool _fast, double _muxLead = 27.5);
		void emitDcoMode(double _time, uint32_t _v, uint32_t _d, uint8_t _mode);
		void emitDcoCount(double _time, uint32_t _v, uint32_t _d, uint32_t _count);
		void emitLatches(double _time);
		void scanSync(uint64_t _until);
		void patchLoad();
		double loadModulatorUs() const;
		void applyInserts();
		void smoothStep(double _time, double _dtUs);
		void emit(double _time, Target _t, uint16_t _channel, uint32_t _value);
		void sortWrites(size_t _first);
		uint32_t cvCode(uint32_t _v, CvIndex _i, const VoiceControls& _c) const;
		int32_t keyPitch(uint32_t _v) const;
		KeyboardMode effectiveMode() const;

		Master m_master;
		std::array<Patch, PatchCount> m_patches;
		Patch m_edit;
		uint32_t m_current = 0;
		uint32_t m_bank = 0;

		MidiIn m_midi;
		MidiMessage m_msg;
		Fifo<QueuedByte> m_inQueue{4096};
		std::vector<uint8_t> m_midiOut;

		VoiceAllocator m_alloc;
		std::vector<VoiceAllocator::Event> m_allocEvents;
		std::array<VoiceState, VoiceCount> m_voices;
		Vibrato m_vibrato;
		Mod m_vibratoValue = 0;
		bool m_lfoExtPending = false;
		bool m_lfoBootProgram = true;   // no 0D loaded yet
		int m_lfoSpeedOverride[2] = {-1, -1};
		int m_vibLatch[3] = {};   // speed, wave, multiplier of the previous block
		std::vector<bool> m_envExtPending;
		double m_envLoadUs = -1.0;
		static double bootSustainUs(uint32_t _v);

		// performance state as matrix source levels (matrix.h)
		Mod m_bend = 0;
		uint8_t m_bend8 = 128;
		Mod m_lever2 = 0, m_lever3 = 0, m_pedal1 = 0, m_pedal2 = 0, m_pressure = 0;
		uint8_t m_cc7 = 0xff;
		int m_volume = 255;   // VCA2 scale 0..255, 255 until the first CC7
		int m_vcfByEnv1 = 0, m_fmByEnv3 = 0;
		bool m_pedal2Down = false;
		bool m_bankLever = false;   // CC1 at bank select position
		bool m_bankLatch = false;   // CC31 bank change enable
		int m_rpn = 0x3fff, m_nrpn = 0x3fff;
		bool m_nrpnSelected = false;
		matrix::Active m_active;

		GlobalControls m_global;
		uint8_t m_lastCtrl = 0xff;
		int m_lastNoise = -1;
		int m_lastRange[VoiceCount * 2];

		double m_now = 0.0;

		OutputMode m_mode = OutputMode::Vintage;
		double m_smoothStepUs = 1e6 / 44100.0;
		double m_nextSmooth = 0.0;
		std::array<uint8_t, VoiceCount> m_order{};    // service list, cyclic
		uint32_t m_pos = 0;                            // index in m_order of the next block
		double m_nextBlock = 0.0;
		double m_lastPoll = 0.0;
		double m_lastGlobal = 0.0;
		uint32_t m_passVoice = 5;                          // voice whose block gets the per-pass extra time (the last note restart)
		bool m_passLate = false;
		bool m_patchLoaded = false;
		std::vector<uint32_t> m_newAlloc;
		double m_cpu = 0.0;                            // scheduler CPU time cursor
		Fifo<std::pair<double, double>> m_byteCost{4096};   // byte arrival time, receive cost
		bool m_byteCostSysex = false;
		uint32_t m_sysexBytes = 0;
		double m_lastF7 = -1.0;
		double m_programLoadAt = -1.0;   // program change: patch load anchored here
		bool m_stalled = false;
		bool m_modeEdited = false;   // keyboard mode changed by opcode 06 since the last patch load
		bool m_afterLoad = false;
		bool m_loadRestart = false;
		bool m_postLoadEditDone = false;
		bool m_coldBoot = true;                        // first patch load after reset
		bool m_bootTableRestart[VoiceCount][2] = {};
		DcoPhase m_phase[VoiceCount][2];
		matrixLib::Pit8254 m_pit[4];
		bool m_mirrorBoot = false;
		uint8_t m_mirrorCtrl = 0xff;
		uint64_t m_syncScanned[VoiceCount] = {};
		bool m_syncLevel[VoiceCount] = {};   // the mirror holds the rig's boot counter state until the first load   // mirror of the DCO timers, for the restart read back (same model as the voice board)
		std::vector<HwWrite> m_writes;
		const analog::Unit* m_analog = nullptr;
	};
}
