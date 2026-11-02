#pragma once
#include <array>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

#include "Alloc.h"
#include "Controllers.h"
#include "Global.h"
#include "Lfo.h"
#include "Midi.h"
#include "Patch.h"
#include "PitchEg.h"
#include "Portamento.h"
#include "Regs.h"

namespace dx7clean
{
// controller CPU cycle offsets of the observed write schedule (fits/timing/FIT.md)
struct Timing
{
	uint32_t uartFirst = 5;
	uint32_t uartByte = 401;
	uint32_t rxIrq = 51;
	uint32_t passPeriod = 3152;
	uint32_t passPeriodHundredths = 76;
	uint32_t passVoiceStart = 270;
	uint32_t passVoiceStep = 172;
	uint32_t passLoByte = 5;
	uint32_t passPitchModShort = 1795;
	uint32_t passPitchModLong = 2533;
	uint32_t passPitchModSkipped = 287;
	uint32_t pollAfterShort = 2330;
	uint32_t pollStep = 161;
	uint32_t skipThresholdNoteOn = 740;
	uint32_t skipThresholdOther = 714;
	uint32_t afterLongFirstWrite = 2568;
	uint32_t inPassFirstWrite = 323;
	uint32_t inPassAfterLong = 806;
	uint32_t inPassAfterSkip = 573;
	uint32_t inPassAfterDelayed = 1050;
	uint32_t chainGap = 725;
	uint32_t monoEarlier = 0;
	uint32_t delayedPassGap = 216;
	uint32_t msgLatency = 470;
	uint32_t keyOffLatency = 304;
	uint32_t keyOffStep = 45;
	uint32_t polyPrePitch = 197;
	uint32_t polyLevels = 772;
	uint32_t polyPitch = 1328;
	uint32_t polyKeyOn = 1357;
	uint32_t monoLevels = 564;
	uint32_t monoPitch = 1126;
	uint32_t monoKeyOn = 1144;
	uint32_t levelStep = 98;
	uint32_t legatoBusy = 400;
	uint32_t miscBusy = 300;
	uint32_t programChangeDelay = 0;
	uint32_t fnResetDelay = 4200;
	uint32_t fnBusy = 15900;
	uint32_t fnTripletStep = 45;
	uint32_t patchPauseExtra = 16140;
	bool patchResetsGroup = true;
	bool ctrlInSkipPass = true;
};

class Core : private MidiHandler
{
public:
	Core();

	std::function<void(uint16_t addr, uint8_t value, uint64_t cycle)> onWrite;

	// power-on: writes the edit buffer like a program change at cycle 0
	void reset();

	void midiIn(const uint8_t* data, size_t len);
	void advance(uint32_t cycles);
	uint64_t cycle() const { return m_now; }

	void setChannel(uint8_t ch) { m_channel = ch & 15; }
	void setMasterTune(int mt) { m_global.masterTune = mt < -256 ? -256 : mt > 255 ? 255 : mt; }
	void setFunction(uint8_t param, uint8_t value);
	// a value already held in function memory at power-on: no voice reset, EG bias stays off
	void presetFunction(uint8_t param, uint8_t value) { applyFunction(param, value, false); }
	void setAllocPolicy(std::unique_ptr<AllocPolicy> p);
	void setAcceptSingleVoiceSysex(bool b) { m_acceptVced = b; }

	void storeInternal(int slot, const Vced& v);
	const Vced& internal(int slot) const { return m_internal[size_t(slot & 31)]; }
	const Vced& editBuffer() const { return m_edit; }
	const Global& global() const { return m_global; }
	Timing& timing() { return m_t; }

	// test hook: move the control pass grid (cycle in Core time) and set the type of that pass
	void alignPass(uint64_t c, bool longPass, int group = -1);

private:
	struct Msg
	{
		uint64_t first;
		uint64_t time;
		uint8_t status;
		uint8_t d1, d2;
		std::vector<uint8_t> sysex;
	};

	struct VoiceState
	{
		uint8_t note = 0;
		uint8_t velocity = 0;
		bool keyed = false;
		bool lowKey = false;
		bool alt = false;
		uint16_t target = 0;
	};

	void onNoteOn(uint8_t ch, uint8_t note, uint8_t vel) override;
	void onNoteOff(uint8_t ch, uint8_t note, uint8_t vel) override;
	void onControlChange(uint8_t ch, uint8_t cc, uint8_t value) override;
	void onProgramChange(uint8_t ch, uint8_t program) override;
	void onChannelPressure(uint8_t ch, uint8_t value) override;
	void onPitchBend(uint8_t ch, uint16_t value14) override;
	void onSysex(const uint8_t* data, size_t len) override;

	void queue(uint8_t status, uint8_t d1, uint8_t d2);
	uint64_t runPass(uint64_t t);
	uint32_t firstWriteLatency(const Msg& m) const;
	static bool isControllerMsg(const Msg& m);
	uint32_t inPassSteps(const Msg& m, int64_t rel) const;
	uint64_t runMsg(const Msg& m, uint64_t t);
	uint64_t runActions(uint64_t t);
	uint64_t runSysex(const std::vector<uint8_t>& s, uint64_t t);
	uint64_t loadPatch(uint64_t t);
	uint64_t voiceTriplets(uint64_t t, uint32_t step);
	uint64_t paramChange(uint16_t p, uint8_t v, uint64_t t);
	void applyFunction(uint8_t param, uint8_t value, bool live = true);
	static bool resetsVoices(uint8_t param);
	uint64_t functionReset(uint64_t t);
	uint16_t voiceWord(int v);
	uint16_t keyTarget(uint8_t note) const;
	void emit(uint64_t cycle, uint16_t addr, uint8_t value);
	void flush(uint64_t end);

	Timing m_t;
	MidiParser m_parser;
	uint8_t m_channel = 0;
	bool m_acceptVced = true;
	bool m_started = false;

	uint64_t m_now = 0;
	uint64_t m_cpuFree = 0;
	uint64_t m_nextPass = 0;
	uint64_t m_uartFree = 0;
	uint64_t m_byteTime = 0;
	uint64_t m_msgFirst = 0;
	bool m_msgOpen = false;
	uint32_t m_passFrac = 0;
	bool m_nextLong = false;
	int m_pegGroup = 1;
	enum class PassKind : uint8_t { Skip, Short, Long };
	PassKind m_lastKind = PassKind::Short;
	bool m_busyFromMsg = false;
	struct Poll
	{
		uint64_t time;
		bool noteOn;
	};
	std::vector<Poll> m_polls;
	uint64_t m_deferAt = 0;

	struct Pending
	{
		uint64_t cycle;
		uint16_t addr;
		uint8_t value;
	};
	std::deque<Pending> m_out;
	std::deque<Msg> m_msgs;

	Vced m_edit{};
	uint8_t m_opMask = 0x3f;
	std::array<Vced, kVoicesPerBank> m_internal{};
	Global m_global;
	bool m_pedal = false;
	bool m_portaPedal = true;

	std::unique_ptr<AllocPolicy> m_alloc;
	std::vector<AllocAction> m_actions;
	std::array<VoiceState, regs::kVoices> m_voices{};

	Lfo m_lfo;
	peg::Bank m_peg;
	Portamento m_porta;
	Controllers m_ctrl;
};
}
