#include "Core.h"

#include "Eg.h"
#include "Level.h"
#include "Pitch.h"

namespace dx7clean
{
namespace
{
constexpr uint8_t voiceEvent(int v, bool on) { return uint8_t(v << 2 | (on ? 1 : 2)); }

// program change schedule, cycles from the start of processing (fits/timing/FIT.md)
struct PatchTiming
{
	static constexpr uint32_t levels = 0, levelStep = 56;
	static constexpr uint32_t triplets = 5398, tripletStep = 53;
	static constexpr uint32_t rates = 8147, rateStep = 125;
	static constexpr uint32_t egLevels = 11466, egLevelStep = 166;
	static constexpr uint32_t opPitch = 39173, opPitchStep = 170, opPitchLo = 6;
	static constexpr uint32_t sens = 40353, sensStep = 106;
	static constexpr uint32_t detune = 41026, detuneStep = 117;
	static constexpr uint32_t ops = 42041;
	static constexpr uint32_t done = 51140;
};
}

Core::Core() : m_parser(*this), m_alloc(std::make_unique<PolyAlloc>())
{
	m_edit = initVoice();
	for (auto& v : m_internal)
		v = initVoice();
	m_peg.reset();
}

void Core::reset()
{
	m_out.clear();
	m_msgs.clear();
	m_parser.reset();
	m_now = m_cpuFree = m_uartFree = m_deferAt = 0;
	m_polls.clear();
	m_msgOpen = m_busyFromMsg = false;
	m_alloc->reset();
	m_peg.reset();
	m_pegGroup = 1;
	for (int v = 0; v < regs::kVoices; ++v)
		pitch::writeVoiceWord(v, peg::applyOffset(pitch::kPowerOnKeyWord, m_peg[v].value()), [this](uint16_t a, uint8_t x) { emit(0, a, x); });
	emit(0, regs::kVolumeDac, Controllers::volumeDac(127));
	// power-on state is complete at cycle 0 (the reference's time-0 snapshot), the CPU is free from there
	loadPatch(0);
	for (auto& w : m_out)
		w.cycle = 0;
	m_cpuFree = m_nextPass = 0;
	m_started = true;
}

void Core::alignPass(uint64_t c, bool longPass, int group)
{
	m_nextPass = c;
	m_passFrac = 0;
	m_nextLong = longPass;
	if (group >= 0 && group != m_pegGroup)
	{
		m_peg.halfTick(m_edit, [this](int v) { return voiceWord(v); }, [](uint16_t, uint8_t) {});
		m_pegGroup ^= 1;
	}
}

void Core::setAllocPolicy(std::unique_ptr<AllocPolicy> p)
{
	if (p)
		m_alloc = std::move(p);
}

void Core::storeInternal(int slot, const Vced& v)
{
	m_internal[size_t(slot & 31)] = v;
	clamp(m_internal[size_t(slot & 31)]);
}

void Core::emit(uint64_t cycle, uint16_t addr, uint8_t value)
{
	m_out.push_back({cycle, addr, value});
}

void Core::flush(uint64_t end)
{
	while (!m_out.empty() && m_out.front().cycle < end)
	{
		const auto w = m_out.front();
		m_out.pop_front();
		if (onWrite)
			onWrite(w.addr, w.value, w.cycle);
	}
}

void Core::midiIn(const uint8_t* data, size_t len)
{
	if (!m_started)
		reset();
	for (size_t i = 0; i < len; ++i)
	{
		const uint64_t a = m_now + m_t.uartFirst, b = m_uartFree + m_t.uartByte;
		m_byteTime = m_uartFree = a > b ? a : b;
		if (!m_msgOpen || data[i] & 0x80)
			m_msgFirst = m_byteTime;
		m_msgOpen = true;

		// the receive interrupt delays whatever the CPU is doing at that moment
		for (auto& w : m_out)
			if (w.cycle > m_byteTime)
				w.cycle += m_t.rxIrq;
		if (m_cpuFree > m_byteTime)
			m_cpuFree += m_t.rxIrq;

		m_parser.feed(data[i]);
	}
}

void Core::queue(uint8_t status, uint8_t d1, uint8_t d2)
{
	m_msgs.push_back({m_msgFirst, m_byteTime, status, d1, d2, {}});
	m_msgOpen = false;
}

void Core::onNoteOn(uint8_t ch, uint8_t note, uint8_t vel)
{
	if (ch == m_channel)
		queue(0x90, note, vel);
}

void Core::onNoteOff(uint8_t ch, uint8_t note, uint8_t)
{
	if (ch == m_channel)
		queue(0x80, note, 0);
}

void Core::onControlChange(uint8_t ch, uint8_t cc, uint8_t value)
{
	if (ch == m_channel)
		queue(0xb0, cc, value);
}

void Core::onProgramChange(uint8_t ch, uint8_t program)
{
	if (ch == m_channel)
		queue(0xc0, program, 0);
}

void Core::onChannelPressure(uint8_t ch, uint8_t value)
{
	if (ch == m_channel)
		queue(0xd0, value, 0);
}

void Core::onPitchBend(uint8_t ch, uint16_t value14)
{
	if (ch == m_channel)
		queue(0xe0, uint8_t(value14 & 0x7f), uint8_t(value14 >> 7));
}

void Core::onSysex(const uint8_t* data, size_t len)
{
	m_msgs.push_back({m_msgFirst, m_byteTime, 0xf0, 0, 0, std::vector<uint8_t>(data, data + len)});
	m_msgOpen = false;
}

void Core::advance(uint32_t cycles)
{
	if (!m_started)
		reset();
	const uint64_t end = m_now + cycles;
	for (;;)
	{
		uint64_t tPass = m_nextPass;
		if (m_cpuFree > tPass)
			tPass = m_busyFromMsg ? m_cpuFree + m_t.delayedPassGap : m_cpuFree;

		uint64_t tPoll = UINT64_MAX;
		if (!m_msgs.empty())
		{
			const Msg& m = m_msgs.front();
			for (const auto& p : m_polls)
				if (p.time >= m_cpuFree && p.time >= m.time && p.time < tPass && (p.noteOn || m.status != 0x90))
				{
					tPoll = p.time;
					break;
				}
			if (m_deferAt && m.time <= m_deferAt && m_deferAt >= m_cpuFree && m_deferAt < tPoll)
				tPoll = m_deferAt;
			if (m_busyFromMsg && m.time <= m_cpuFree)
			{
				const uint64_t tc = m_cpuFree + m_t.chainGap - firstWriteLatency(m);
				if (tc < tPoll && tc < tPass)
					tPoll = tc;
			}
		}

		const uint64_t t = tPoll <= tPass ? tPoll : tPass;
		if (t >= end)
			break;
		if (tPoll <= tPass)
		{
			const Msg m = std::move(m_msgs.front());
			m_msgs.pop_front();
			if (t == m_deferAt)
			{
				m_deferAt = 0;
				m_cpuFree = runMsg(m, t - firstWriteLatency(m));
			}
			else
				m_cpuFree = runMsg(m, t);
			m_busyFromMsg = true;
		}
		else
		{
			m_cpuFree = runPass(tPass);
			m_passFrac += m_t.passPeriodHundredths;
			m_nextPass = tPass + m_t.passPeriod + m_passFrac / 100;
			m_passFrac %= 100;
		}
	}
	flush(end);
	m_now = end;
}

bool Core::isControllerMsg(const Msg& m)
{
	if (m.status == 0xe0 || m.status == 0xd0)
		return true;
	return m.status == 0xb0 && (m.d1 == 1 || m.d1 == 2 || m.d1 == 4);
}

uint32_t Core::firstWriteLatency(const Msg& m) const
{
	if (m.status == 0x90)
		return m_global.mono ? m_t.msgLatency - m_t.monoEarlier : m_t.msgLatency;
	if (m.status == 0x80)
		return m_t.keyOffLatency;
	return m_t.msgLatency;
}

// polls of a skipped pass the message needs before it is serviced; thresholds relative to the pass start
uint32_t Core::inPassSteps(const Msg& m, int64_t rel) const
{
	static constexpr int64_t kOn[] = {-670, -470, -365, -65, 129, 531};
	static constexpr int64_t kOther[] = {-513, -409, -115, 94, 494};
	const int64_t* th = m.status == 0x90 ? kOn : kOther;
	const size_t n = m.status == 0x90 ? 6 : 5;
	uint32_t k = 0;
	while (k < n && rel > th[k])
		++k;
	if (k == n && rel > th[n - 1])
		k += uint32_t((rel - th[n - 1] + m_t.pollStep - 1) / m_t.pollStep) - 1;
	return k;
}

uint16_t Core::keyTarget(uint8_t note) const
{
	return pitch::voiceWord(note, m_edit[vced::TRNP], m_global.masterTune);
}

uint16_t Core::voiceWord(int v)
{
	uint16_t w = m_porta.update(v, m_global);
	// keys 0 and 1 alternate with the word 255 lower on every refresh (timing/keys_0_1)
	auto& vs = m_voices[size_t(v)];
	if (vs.lowKey && w == vs.target)
	{
		vs.alt = !vs.alt;
		if (vs.alt && w >= 255)
			w = uint16_t(w - 255);
	}
	return w;
}

uint64_t Core::runPass(uint64_t t)
{
	const bool isLong = m_nextLong;
	m_nextLong = !m_nextLong;
	const bool forced = m_busyFromMsg && m_cpuFree + m_t.delayedPassGap >= t && t > m_nextPass + 20;
	bool skip = forced;

	const Msg* pending = nullptr;
	if (!m_msgs.empty() && m_msgs.front().first <= t)
	{
		pending = &m_msgs.front();
		const uint32_t thr = pending->status == 0x90 ? m_t.skipThresholdNoteOn : m_t.skipThresholdOther;
		if (m_lastKind != PassKind::Short || pending->time + thr > t)
			skip = true;
		else if (isLong)
			m_deferAt = t + m_t.afterLongFirstWrite;
	}
	m_polls.clear();

	// controller values pending at a skipped pass already feed this pass's modulation
	if (skip && m_t.ctrlInSkipPass)
		while (!m_msgs.empty() && m_msgs.front().first <= t && m_msgs.front().time <= t + m_t.passPitchModSkipped && isControllerMsg(m_msgs.front()))
		{
			const Msg m = std::move(m_msgs.front());
			m_msgs.pop_front();
			runMsg(m, t);
		}

	m_lfo.tick(m_ctrl, m_global);
	emit(t, regs::kAmpMod, m_ctrl.ampModReg(m_lfo, m_edit, m_global));

	uint32_t pm = m_t.passPitchModSkipped;
	if (!skip)
	{
		int i = 0;
		m_peg.halfTick(m_edit, [this](int v) { return voiceWord(v); }, [&](uint16_t addr, uint8_t value) {
			const uint64_t c = t + m_t.passVoiceStart + uint64_t(i / 2) * m_t.passVoiceStep + (i & 1) * m_t.passLoByte;
			emit(c, addr, value);
			++i;
		});
		m_pegGroup ^= 1;
		pm = isLong ? m_t.passPitchModLong : m_t.passPitchModShort;
	}

	const auto w = uint16_t(m_ctrl.pitchModReg(m_lfo, m_edit, m_global));
	emit(t + pm, regs::kPitchMod, uint8_t(w >> 8));
	emit(t + pm + 4, regs::kPitchMod + 1, uint8_t(w & 0xff));
	uint64_t free = t + pm + 10;

	if (skip)
	{
		const PassKind prev = m_lastKind;
		m_lastKind = PassKind::Skip;
		m_deferAt = 0;
		if (!m_msgs.empty() && m_msgs.front().first <= t)
		{
			const Msg m = std::move(m_msgs.front());
			m_msgs.pop_front();
			uint64_t f1 = t + m_t.inPassFirstWrite + uint64_t(inPassSteps(m, int64_t(m.time) - int64_t(t))) * m_t.pollStep;
			uint64_t floor = 0;
			if (forced)
				floor = t + m_t.inPassAfterDelayed;
			else if (prev == PassKind::Long)
				floor = t + m_t.inPassAfterLong;
			else if (prev == PassKind::Skip)
				floor = t + m_t.inPassAfterSkip;
			if (f1 < floor)
				f1 = floor;
			free = runMsg(m, f1 - firstWriteLatency(m));
			m_busyFromMsg = true;
			return free;
		}
		m_busyFromMsg = false;
		return free;
	}

	m_lastKind = isLong ? PassKind::Long : PassKind::Short;
	if (!isLong)
	{
		m_polls.push_back({t + m_t.pollAfterShort, true});
		m_polls.push_back({t + m_t.pollAfterShort + m_t.pollStep, false});
	}
	m_busyFromMsg = false;
	return free;
}

uint64_t Core::runMsg(const Msg& m, uint64_t t)
{
	m_actions.clear();
	switch (m.status)
	{
	case 0x90:
	{
		const bool firstKey = !m_alloc->anyKeyHeld();
		m_alloc->noteOn(m.d1, m.d2, m_actions);
		for (const auto& a : m_actions)
			if (a.type == AllocAction::KeyOn)
				m_lfo.keyOn(firstKey);
		return runActions(t);
	}
	case 0x80:
		m_alloc->noteOff(m.d1, m_actions);
		return runActions(t);
	case 0xb0:
		switch (m.d1)
		{
		case 1: m_ctrl.wheel(m.d2); break;
		case 2: m_ctrl.breath(m.d2); break;
		case 4: m_ctrl.foot(m.d2); break;
		case 7:
			emit(t + m_t.msgLatency, regs::kVolumeDac, Controllers::volumeDac(m.d2));
			break;
		case 64:
			m_pedal = m.d2 >= 64;
			m_alloc->sustain(m_pedal, m_actions);
			return runActions(t);
		case 65:
			m_portaPedal = m.d2 >= 64;
			m_porta.setPedal(m_portaPedal);
			break;
		case 123:
			m_alloc->allNotesOff(m_actions);
			return runActions(t);
		default: break;
		}
		return t + m_t.miscBusy;
	case 0xc0:
		if (m.d1 >= kVoicesPerBank)
			return t + m_t.miscBusy;
		m_edit = m_internal[m.d1];
		m_opMask = 0x3f;
		{
			// the reference makes its first patch write about 17.8 ms after the program change is sent
			const uint64_t a = t + m_t.msgLatency, b = m.first + m_t.programChangeDelay;
			return loadPatch(a > b ? a : b);
		}
	case 0xd0:
		m_ctrl.aftertouch(m.d1);
		return t + m_t.miscBusy;
	case 0xe0:
		m_ctrl.pitchBend(uint16_t(m.d1 | m.d2 << 7));
		return t + m_t.miscBusy;
	case 0xf0:
		return runSysex(m.sysex, t);
	default:
		return t;
	}
}

uint64_t Core::runActions(uint64_t t)
{
	if (m_actions.empty())
		return t + m_t.miscBusy;

	const bool mono = m_global.mono;
	uint64_t offT = t + m_t.keyOffLatency;
	uint64_t free = t + m_t.miscBusy;

	for (const auto& a : m_actions)
	{
		const int v = a.voice;
		auto& vs = m_voices[size_t(v)];
		switch (a.type)
		{
		case AllocAction::KeyOff:
			emit(offT, regs::kVoiceEvent, voiceEvent(v, false));
			m_peg[v].keyOff();
			m_porta.keyOff(v);
			vs.keyed = false;
			free = offT + m_t.keyOffStep;
			offT += m_t.keyOffStep;
			break;
		case AllocAction::Legato:
			vs.note = a.note;
			vs.target = keyTarget(a.note);
			vs.lowKey = pitch::transposedKey(a.note, m_edit[vced::TRNP]) >= 0 && pitch::transposedKey(a.note, m_edit[vced::TRNP]) <= 1;
			vs.alt = false;
			m_porta.noteOn(v, vs.target, true, m_global);
			free = t + m_t.legatoBusy;
			break;
		case AllocAction::KeyOn:
		{
			const uint64_t t0 = t + m_t.msgLatency;
			auto write = [this](uint64_t c, uint16_t addr, uint8_t value) { emit(c, addr, value); };
			emit(t0, regs::kVoiceEvent, voiceEvent(v, false));

			vs.note = a.note;
			vs.velocity = a.velocity;
			vs.keyed = true;
			vs.target = keyTarget(a.note);
			vs.lowKey = pitch::transposedKey(a.note, m_edit[vced::TRNP]) >= 0 && pitch::transposedKey(a.note, m_edit[vced::TRNP]) <= 1;
			vs.alt = false;
			m_porta.noteOn(v, vs.target, false, m_global);

			if (!mono)
			{
				const uint16_t pre = peg::applyOffset(m_porta.current(v), m_peg[v].value());
				write(t0 + m_t.polyPrePitch, uint16_t(regs::kVoicePitch + 2 * v), uint8_t(pre >> 8));
				write(t0 + m_t.polyPrePitch + 7, uint16_t(regs::kVoicePitch + 2 * v + 1), uint8_t(pre & 0xff));
			}
			m_peg[v].keyOn(m_edit);

			const uint64_t tl = t0 + (mono ? m_t.monoLevels : m_t.polyLevels);
			int li = 0;
			level::writeOutputLevels(m_edit, m_opMask, v, a.note, a.velocity, [&](uint16_t addr, uint8_t value) {
				emit(tl + uint64_t(li++) * m_t.levelStep, addr, value);
			});

			const uint64_t tp = t0 + (mono ? m_t.monoPitch : m_t.polyPitch);
			const uint16_t w = peg::applyOffset(m_porta.current(v), m_peg[v].value());
			write(tp, uint16_t(regs::kVoicePitch + 2 * v), uint8_t(w >> 8));
			write(tp + 7, uint16_t(regs::kVoicePitch + 2 * v + 1), uint8_t(w & 0xff));

			const uint64_t tk = t0 + (mono ? m_t.monoKeyOn : m_t.polyKeyOn);
			emit(tk, regs::kVoiceEvent, voiceEvent(v, true));
			free = tk + 20;
			break;
		}
		}
	}
	return free;
}

uint64_t Core::voiceTriplets(uint64_t t, uint32_t step)
{
	for (int v = 0; v < regs::kVoices; ++v)
	{
		emit(t, regs::kVoiceEvent, voiceEvent(v, false));
		emit(t + step, regs::kVoiceEvent, voiceEvent(v, true));
		emit(t + 2 * step, regs::kVoiceEvent, voiceEvent(v, false));
		t += 3 * step;
	}
	for (auto& vs : m_voices)
		vs.keyed = false;
	return t;
}

uint64_t Core::loadPatch(uint64_t t)
{
	using P = PatchTiming;
	clamp(m_edit);

	for (int i = 0; i < regs::kOps * regs::kVoices; ++i)
		emit(t + P::levels + uint64_t(i) * P::levelStep, uint16_t(regs::kOpLevel + i), 255);

	std::vector<AllocAction> discard;
	m_alloc->allNotesOff(discard);
	m_alloc->sustain(false, discard);
	m_alloc->sustain(m_pedal, discard);
	voiceTriplets(t + P::triplets, P::tripletStep);

	int i = 0;
	eg::writeRatesLevels(m_edit, [&](uint16_t addr, uint8_t value) {
		const uint64_t c = i < 24 ? t + P::rates + uint64_t(i) * P::rateStep : t + P::egLevels + uint64_t(i - 24) * P::egLevelStep;
		emit(c, addr, value);
		++i;
	});

	i = 0;
	pitch::writeOpPitch(m_edit, [&](uint16_t addr, uint8_t value) {
		emit(t + P::opPitch + uint64_t(i / 2) * P::opPitchStep + (i & 1) * P::opPitchLo, addr, value);
		++i;
	});

	i = 0;
	eg::writeSens(m_edit, [&](uint16_t addr, uint8_t value) { emit(t + P::sens + uint64_t(i++) * P::sensStep, addr, value); });

	i = 0;
	pitch::writeDetune(m_edit, [&](uint16_t addr, uint8_t value) { emit(t + P::detune + uint64_t(i++) * P::detuneStep, addr, value); });

	const uint8_t mode = uint8_t(regs::kModeAllVoices | (m_edit[vced::OKS] ? regs::kModeKeySyncOn : regs::kModeKeySyncOff));
	emit(t + P::ops, regs::kOpsMode, mode);
	emit(t + P::ops + 3, regs::kOpsAlg, uint8_t((m_edit[vced::ALG] & 31) << 3 | (m_edit[vced::FB] & 7)));

	m_lfo.setPatch(m_edit);
	if (m_t.patchResetsGroup && m_pegGroup != 1)
		alignPass(m_nextPass, m_nextLong, 1);
	return t + P::done + m_t.patchPauseExtra;
}

uint64_t Core::paramChange(uint16_t p, uint8_t value, uint64_t t)
{
	t += m_t.msgLatency;
	if (p == vced::kOpMaskIndex)
	{
		m_opMask = value & 0x3f;
		return t;
	}
	if (p >= kVcedSize)
		return t;

	m_edit[p] = value;
	clamp(m_edit);

	uint32_t n = 0;
	auto w = [&](uint16_t addr, uint8_t val) { emit(t + 50 * n++, addr, val); };

	if (p < 126)
	{
		const int o = int(p / kOpBlockSize);
		const int f = int(p % kOpBlockSize);
		if (f <= vced::R4)
			for (int s = 0; s < 4; ++s)
				w(uint16_t(regs::kEgRate + 4 * o + s), eg::rateReg(m_edit[vced::op(o, vced::Op(vced::R1 + s))]));
		else if (f <= vced::L4)
			for (int s = 0; s < 4; ++s)
				w(uint16_t(regs::kEgLevel + 4 * o + s), eg::levelReg(m_edit[vced::op(o, vced::Op(vced::L1 + s))]));
		else if (f == vced::RS || f == vced::AMS)
			w(uint16_t(regs::kOpSens + o), eg::sensReg(m_edit[vced::op(o, vced::RS)], m_edit[vced::op(o, vced::AMS)]));
		else if (f == vced::MODE || f == vced::FC || f == vced::FF)
		{
			const uint16_t word = pitch::opWord(m_edit, o);
			w(uint16_t(regs::kOpPitch + 2 * o), uint8_t(word >> 8));
			w(uint16_t(regs::kOpPitch + 2 * o + 1), uint8_t(word & 0xff));
		}
		else if (f == vced::DT)
			w(uint16_t(regs::kOpDetune + o), pitch::detuneReg(m_edit[vced::op(o, vced::DT)]));
	}
	else if (p >= vced::ALG && p <= vced::OKS)
	{
		w(regs::kOpsMode, uint8_t(regs::kModeAllVoices | (m_edit[vced::OKS] ? regs::kModeKeySyncOn : regs::kModeKeySyncOff)));
		w(regs::kOpsAlg, uint8_t((m_edit[vced::ALG] & 31) << 3 | (m_edit[vced::FB] & 7)));
	}
	else if (p >= vced::LFS && p <= vced::PMS)
		m_lfo.setPatch(m_edit);

	return t + 50 * n + m_t.miscBusy;
}

void Core::setFunction(uint8_t param, uint8_t value)
{
	applyFunction(param, value);
	if (resetsVoices(param))
	{
		const uint64_t t = (m_cpuFree > m_now ? m_cpuFree : m_now) + m_t.msgLatency;
		m_cpuFree = functionReset(t);
		m_busyFromMsg = true;
	}
}

bool Core::resetsVoices(uint8_t param)
{
	return param == fn::PolyMono || param == fn::PortaMode || param == fn::Glissando || param == fn::PortaTime;
}

// poly/mono and portamento changes: key off/on/off on every voice and all key words back to 21504
uint64_t Core::functionReset(uint64_t t)
{
	const uint64_t end = voiceTriplets(t, m_t.fnTripletStep);
	m_porta.resetVoices();
	for (auto& vs : m_voices)
	{
		vs.target = pitch::kPowerOnKeyWord;
		vs.lowKey = vs.alt = false;
	}
	return end;
}

void Core::applyFunction(uint8_t param, uint8_t value, bool live)
{
	auto& g = m_global;
	if (live)
		m_ctrl.functionChanged();
	switch (param)
	{
	case fn::PolyMono:
	{
		const bool mono = value != 0;
		if (mono != g.mono)
		{
			g.mono = mono;
			if (mono)
				m_alloc = std::make_unique<MonoAlloc>();
			else
				m_alloc = std::make_unique<PolyAlloc>();
		}
		break;
	}
	case fn::BendRange: g.bendRange = value > 12 ? 12 : value; break;
	case fn::BendStep: g.bendStep = value > 12 ? 12 : value; break;
	case fn::PortaMode: g.portaMode = value & 1; break;
	case fn::Glissando: g.glissando = value != 0; break;
	case fn::PortaTime: g.portaTime = value > 99 ? 99 : value; break;
	case fn::WheelRange: g.wheel.range = value > 99 ? 99 : value; break;
	case fn::WheelAssign: g.wheel.assign = value & 7; break;
	case fn::FootRange: g.foot.range = value > 99 ? 99 : value; break;
	case fn::FootAssign: g.foot.assign = value & 7; break;
	case fn::BreathRange: g.breath.range = value > 99 ? 99 : value; break;
	case fn::BreathAssign: g.breath.assign = value & 7; break;
	case fn::AtRange: g.aftertouch.range = value > 99 ? 99 : value; break;
	case fn::AtAssign: g.aftertouch.assign = value & 7; break;
	default: break;
	}
}

uint64_t Core::runSysex(const std::vector<uint8_t>& s, uint64_t t)
{
	if (s.size() < 4 || s[1] != 0x43)
		return t + m_t.miscBusy;
	const uint8_t sub = s[2] >> 4, ch = s[2] & 15;
	if (ch != m_channel)
		return t + m_t.miscBusy;

	if (sub == 0 && s.size() >= 8)
	{
		const uint8_t format = s[3];
		const size_t count = size_t(s[4]) << 7 | s[5];
		if (s.size() != count + 8)
			return t + m_t.miscBusy;
		const uint8_t* d = &s[6];
		if (checksum(d, count) != s[6 + count])
			return t + m_t.miscBusy;

		if (format == 0 && count == kVcedSize)
		{
			if (!m_acceptVced)
				return t + m_t.miscBusy;
			std::copy(d, d + kVcedSize, m_edit.begin());
			m_opMask = 0x3f;
			return loadPatch(t + m_t.msgLatency);
		}
		if (format == 9 && count == kVmemSize)
		{
			for (size_t i = 0; i < kVoicesPerBank; ++i)
				m_internal[i] = unpack(d + i * kVmemVoiceSize);
			return t + m_t.miscBusy;
		}
		return t + m_t.miscBusy;
	}

	if (sub == 1 && s.size() == 7)
	{
		const uint8_t group = (s[3] >> 2) & 31;
		const uint16_t p = uint16_t((s[3] & 3) << 7 | s[4]);
		if (group == 0)
			return paramChange(p, s[5], t);
		if (group == 2)
		{
			applyFunction(uint8_t(p), s[5]);
			if (resetsVoices(uint8_t(p)))
			{
				const uint64_t end = functionReset(t + m_t.fnResetDelay);
				const uint64_t busy = t + m_t.fnBusy;
				return busy > end ? busy : end;
			}
			return t + m_t.miscBusy;
		}
	}
	return t + m_t.miscBusy;
}
}
