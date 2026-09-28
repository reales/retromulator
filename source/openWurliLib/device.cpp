/**
 * OpenWurli Device adapter for Retromulator
 * Wraps the OpenWurli Wurlitzer 200A engine as a synthLib::Device
 */
#include "device.h"

#include <cstring>
#include <cmath>
#include <algorithm>

namespace openWurliLib
{

namespace
{
	uint32_t rampSamplesForRate(double sampleRate)
	{
		return std::max(static_cast<uint32_t>(sampleRate * 0.005), 1u);
	}

	double onePoleAlpha(double fc, double sr)
	{
		if (!std::isfinite(fc) || fc >= 0.5 * sr)
			return 1.0;
		return 1.0 - std::exp(-2.0 * M_PI * fc / sr);
	}

	constexpr double kOutputAlignment = 5.011872336272722; // 10^(14/20)
}

Device::Device(const synthLib::DeviceCreateParams& _params)
	: synthLib::Device(_params)
{
	m_sampleRate = 44100.0;
	m_oversample = m_sampleRate < 88200.0;
	m_osSampleRate = m_oversample ? m_sampleRate * 2.0 : m_sampleRate;

	m_preamp.init(m_osSampleRate);
	m_tremolo.init(m_tremoloDepth, m_osSampleRate);
	m_oversampler.init();
	m_powerAmp.init(m_osSampleRate);
	m_speaker.init(m_sampleRate);

	const uint32_t ramp = rampSamplesForRate(m_sampleRate);
	m_volumeSmoother.init(m_volume, ramp);
	m_tremoloDepthSmoother.init(m_tremoloDepth, ramp);
	m_speakerCharacterSmoother.init(m_speakerCharacter, ramp);

	m_voiceBuf.resize(MAX_BLOCK, 0.0);
	m_sumBuf.resize(MAX_BLOCK, 0.0);
	m_upBuf.resize(MAX_BLOCK * 2, 0.0);
	m_outBuf.resize(MAX_BLOCK, 0.0);

	warmUp();
}

Device::~Device()
{
	m_shutdown.store(true);
}

float Device::getSamplerate() const
{
	return static_cast<float>(m_sampleRate);
}

bool Device::isValid() const
{
	return true; // No ROM needed, pure physical model
}

bool Device::setDspClockPercent(uint32_t)
{
	return false;
}

uint64_t Device::getDspClockHz() const
{
	return static_cast<uint64_t>(m_sampleRate);
}

#if SYNTHLIB_DEMO_MODE == 0
bool Device::getState(std::vector<uint8_t>& _state, synthLib::StateType _type)
{
	if (_type == synthLib::StateTypeGlobal)
	{
		// Layout: volume, (reserved/tremRate), tremDepth, speakerChar, mlpEnabled, velocityCurve (all float)
		// Slot 1 kept for backwards compatibility (was tremRate, now ignored on load)
		_state.resize(6 * sizeof(float));
		auto* p = _state.data();
		std::memcpy(p, &m_volume, sizeof(float)); p += sizeof(float);
		float reserved = 5.63f; // backwards compat placeholder
		std::memcpy(p, &reserved, sizeof(float)); p += sizeof(float);
		std::memcpy(p, &m_tremoloDepth, sizeof(float)); p += sizeof(float);
		std::memcpy(p, &m_speakerCharacter, sizeof(float)); p += sizeof(float);
		float mlp = m_mlpEnabled ? 1.0f : 0.0f;
		std::memcpy(p, &mlp, sizeof(float)); p += sizeof(float);
		float vc = static_cast<float>(m_velocityCurve);
		std::memcpy(p, &vc, sizeof(float));
		return true;
	}
	return false;
}

bool Device::setState(const std::vector<uint8_t>& _state, synthLib::StateType _type)
{
	if (_type == synthLib::StateTypeGlobal && _state.size() >= 5 * sizeof(float))
	{
		auto* p = _state.data();
		std::memcpy(&m_volume, p, sizeof(float)); p += sizeof(float);
		p += sizeof(float); // skip slot 1 (was tremRate, removed in 0.3)
		std::memcpy(&m_tremoloDepth, p, sizeof(float)); p += sizeof(float);
		std::memcpy(&m_speakerCharacter, p, sizeof(float)); p += sizeof(float);
		float mlp;
		std::memcpy(&mlp, p, sizeof(float)); p += sizeof(float);
		m_mlpEnabled = mlp > 0.5f;
		// velocityCurve added later, backwards compatible (missing = default)
		if (_state.size() >= 6 * sizeof(float))
		{
			float vc;
			std::memcpy(&vc, p, sizeof(float));
			m_velocityCurve = std::clamp(static_cast<int>(vc + 0.5f), 0, 4);
		}
		else
		{
			m_velocityCurve = kVelocityCurveDefault;
		}
		m_volumeSmoother.snapTo(static_cast<double>(m_volume * m_expression));
		m_tremoloDepthSmoother.snapTo(static_cast<double>(m_tremoloDepth));
		m_speakerCharacterSmoother.snapTo(static_cast<double>(m_speakerCharacter));
		return true;
	}
	return false;
}
#endif

void Device::noteOn(uint8_t note, uint8_t velocity)
{
	if (velocity == 0) { noteOff(note); return; }

	const uint8_t clampedNote = std::clamp(note, openWurli::MIDI_LO, openWurli::MIDI_HI);
	const double rawVel = static_cast<double>(velocity) / 127.0;

	// Apply velocity curve preset
	double vel;
	switch (m_velocityCurve)
	{
	case 0: vel = rawVel;                                     break; // Linear
	case 1: vel = rawVel * rawVel;                            break; // Soft (square)
	case 2: vel = rawVel;                                     break; // Medium (engine S-curve handles it)
	case 3: vel = std::sqrt(rawVel);                          break; // Hard (sqrt, boosted low velocities)
	case 4: vel = 0.75;                                       break; // Fixed (mezzo-forte)
	default: vel = rawVel;                                    break;
	}

	// Re-striking a sustained note releases the old voice first
	// (one reed per pitch on the real 200A).
	for (auto& s : m_voices)
	{
		if (s.state == VoiceState::Sustained && s.midiNote == note)
		{
			s.state = VoiceState::Releasing;
			s.voice.noteOff();
		}
	}

	const size_t slotIdx = allocateVoice();
	auto& slot = m_voices[slotIdx];

	// Voice stealing crossfade
	if (slot.state != VoiceState::Free)
	{
		const uint32_t fadeSamples = static_cast<uint32_t>(m_sampleRate * 0.005);
		slot.stealVoice = slot.voice;
		slot.hasStealVoice = true;
		slot.stealFade = fadeSamples;
		slot.stealFadeLen = fadeSamples;
	}

	m_ageCounter++;
	const uint32_t noiseSeed = static_cast<uint32_t>(note) * 2654435761u + static_cast<uint32_t>(m_ageCounter);
	slot.voice.noteOn(clampedNote, vel, m_sampleRate, noiseSeed, m_mlpEnabled);
	slot.state = VoiceState::Held;
	slot.midiNote = note;
	slot.age = m_ageCounter;
}

void Device::noteOff(uint8_t note)
{
	// Clamp identically to noteOn so an out-of-range note-on can always be released.
	const uint8_t matchNote = std::clamp(note, openWurli::MIDI_LO, openWurli::MIDI_HI);

	// Release oldest held voice matching this note. With the pedal down,
	// Held → Sustained (still ringing); otherwise Held → Releasing.
	size_t bestIdx = MAX_VOICES;
	uint64_t bestAge = UINT64_MAX;

	for (size_t i = 0; i < MAX_VOICES; i++)
	{
		if (m_voices[i].state == VoiceState::Held && m_voices[i].midiNote == matchNote)
		{
			if (m_voices[i].age < bestAge)
			{
				bestAge = m_voices[i].age;
				bestIdx = i;
			}
		}
	}

	if (bestIdx < MAX_VOICES)
	{
		if (m_sustainPedal)
		{
			m_voices[bestIdx].state = VoiceState::Sustained;
		}
		else
		{
			m_voices[bestIdx].state = VoiceState::Releasing;
			m_voices[bestIdx].voice.noteOff();
		}
	}
}

void Device::allNotesOff()
{
	m_sustainPedal = false;

	for (auto& slot : m_voices)
	{
		if (slot.state == VoiceState::Held || slot.state == VoiceState::Sustained)
		{
			slot.state = VoiceState::Releasing;
			slot.voice.noteOff();
		}
	}
}

size_t Device::allocateVoice()
{
	// Priority: Free > oldest Releasing > oldest Sustained > oldest Held.
	size_t bestIdx = 0;
	uint64_t bestPriority = UINT64_MAX;

	for (size_t i = 0; i < MAX_VOICES; i++)
	{
		uint64_t priority;
		switch (m_voices[i].state)
		{
		case VoiceState::Free:      return i;
		case VoiceState::Releasing: priority = m_voices[i].age;                       break;
		case VoiceState::Sustained: priority = m_voices[i].age + UINT64_MAX / 4;      break;
		case VoiceState::Held:      priority = m_voices[i].age + UINT64_MAX / 2;      break;
		}

		if (priority < bestPriority)
		{
			bestPriority = priority;
			bestIdx = i;
		}
	}
	return bestIdx;
}

void Device::resetChain()
{
	m_preamp.reset();
	m_c9State = 0.0;
	m_oversampler.reset();
	m_powerAmp.reset();
	m_speaker.reset();
}

// Settle the preamp / tremolo oscillator / CdS envelope to their steady operating
// point with 0.6 s of internal silence, so the first note never rides a cold-start
// gain excursion.
void Device::warmUp()
{
	const size_t total = static_cast<size_t>(m_sampleRate * 0.6);
	size_t done = 0;
	while (done < total)
	{
		const size_t len = std::min<size_t>(512, total - done);
		renderVoicesToAmpOut(0, len);
		renderOutput(nullptr, nullptr, len);
		done += len;
	}
}

// Sum voices, then run preamp → drawn volume network → power amp inside the
// oversampled bus. Leaves the post-amp signal at base rate in m_outBuf.
void Device::renderVoicesToAmpOut(size_t offset, size_t len)
{
	std::fill(m_sumBuf.begin(), m_sumBuf.begin() + len, 0.0);

	for (auto& slot : m_voices)
	{
		if (slot.state == VoiceState::Free && !slot.hasStealVoice)
			continue;

		if (slot.state != VoiceState::Free)
		{
			slot.voice.render(m_voiceBuf.data(), len);
			for (size_t i = 0; i < len; i++)
				m_sumBuf[i] += m_voiceBuf[i];
		}

		if (slot.hasStealVoice)
		{
			slot.stealVoice.render(m_voiceBuf.data(), len);
			const double fadeLen = static_cast<double>(slot.stealFadeLen);
			for (size_t i = 0; i < len; i++)
			{
				const uint32_t remaining = (slot.stealFade > static_cast<uint32_t>(i))
					? slot.stealFade - static_cast<uint32_t>(i) : 0;
				const double gain = static_cast<double>(remaining) / fadeLen;
				m_sumBuf[i] += m_voiceBuf[i] * gain;
			}
			slot.stealFade = (slot.stealFade > static_cast<uint32_t>(len))
				? slot.stealFade - static_cast<uint32_t>(len) : 0;
			if (slot.stealFade == 0)
				slot.hasStealVoice = false;
		}
	}

	// NaN guard on voice output, before anything with IIR state sees it
	bool hasNan = false;
	for (size_t i = 0; i < len; i++)
	{
		if (!std::isfinite(m_sumBuf[i])) { hasNan = true; break; }
	}
	if (hasNan)
	{
		std::fill(m_sumBuf.begin(), m_sumBuf.begin() + len, 0.0);
		for (auto& slot : m_voices)
		{
			if (slot.state == VoiceState::Free && !slot.hasStealVoice)
				continue;
			if (slot.state != VoiceState::Free)
			{
				slot.voice.render(m_voiceBuf.data(), len);
				for (size_t i = 0; i < len; i++)
				{
					if (!std::isfinite(m_voiceBuf[i]))
					{
						slot.state = VoiceState::Free;
						slot.voice.setInactive();
						break;
					}
				}
			}
			if (slot.hasStealVoice)
			{
				slot.stealVoice.render(m_voiceBuf.data(), len);
				for (size_t i = 0; i < len; i++)
				{
					if (!std::isfinite(m_voiceBuf[i]))
					{
						slot.hasStealVoice = false;
						slot.stealFade = 0;
						break;
					}
				}
			}
		}
	}

	const double r11 = openWurli::R11_REED_BAR_VOLUME_DEFAULT;
	constexpr double ocFactor = openWurli::DkPreamp::openCircuitOutputFactor();

	if (m_oversample)
	{
		m_oversampler.upsample2x(m_sumBuf.data(), m_upBuf.data(), len);

		for (size_t i = 0; i < len; i++)
		{
			m_tremolo.setDepth(m_tremoloDepthSmoother.next());
			const double vol = m_volumeSmoother.next();
			const double driveGain = ocFactor * openWurli::volumePotGain(vol, r11);
			m_c9Alpha = onePoleAlpha(openWurli::volumePotPoleHz(vol, r11), m_osSampleRate);

			for (int j = 0; j < 2; j++)
			{
				const size_t idx = i * 2 + j;
				m_preamp.setLdrResistance(m_tremolo.process());
				const double preampOut = m_preamp.processSample(m_upBuf[idx]);
				m_c9State += m_c9Alpha * (preampOut * driveGain - m_c9State);
				m_upBuf[idx] = m_powerAmp.process(m_c9State);
			}
		}

		m_oversampler.downsample2x(m_upBuf.data(), &m_outBuf[offset], len);
	}
	else
	{
		for (size_t i = 0; i < len; i++)
		{
			m_tremolo.setDepth(m_tremoloDepthSmoother.next());
			m_preamp.setLdrResistance(m_tremolo.process());
			const double preampOut = m_preamp.processSample(m_sumBuf[i]);
			const double vol = m_volumeSmoother.next();
			const double driveGain = ocFactor * openWurli::volumePotGain(vol, r11);
			m_c9Alpha = onePoleAlpha(openWurli::volumePotPoleHz(vol, r11), m_osSampleRate);
			m_c9State += m_c9Alpha * (preampOut * driveGain - m_c9State);
			m_outBuf[offset + i] = m_powerAmp.process(m_c9State);
		}
	}
}

// Speaker + output alignment on m_outBuf. Null outputs discard (warm-up).
void Device::renderOutput(float* outL, float* outR, size_t len)
{
	for (size_t i = 0; i < len; i++)
	{
		m_speaker.setCharacter(m_speakerCharacterSmoother.next());
		const double shaped = m_speaker.process(m_outBuf[i]);
		const double post = shaped * openWurli::POST_SPEAKER_GAIN * kOutputAlignment;

		float sample = static_cast<float>(post);
		if (!std::isfinite(sample))
		{
			resetChain();
			sample = 0.0f;
		}

		if (outL)
		{
			outL[i] = sample;
			outR[i] = sample; // mono → stereo
		}
	}
}

void Device::cleanupVoices()
{
	for (auto& slot : m_voices)
	{
		if (slot.state != VoiceState::Free && slot.voice.isSilent())
		{
			slot.state = VoiceState::Free;
			slot.voice.setInactive();
		}
	}
}

void Device::processAudio(const synthLib::TAudioInputs& /*_inputs*/, const synthLib::TAudioOutputs& _outputs, size_t _samples)
{
	if (m_shutdown.load()) return;

	if (m_outBuf.size() < _samples)
	{
		m_voiceBuf.resize(_samples, 0.0);
		m_sumBuf.resize(_samples, 0.0);
		m_upBuf.resize(_samples * 2, 0.0);
		m_outBuf.resize(_samples, 0.0);
	}

	m_volumeSmoother.setTarget(static_cast<double>(m_volume * m_expression));
	m_tremoloDepthSmoother.setTarget(static_cast<double>(m_tremoloDepth));
	m_speakerCharacterSmoother.setTarget(static_cast<double>(m_speakerCharacter));

	// MIDI events were already dispatched by the base class
	renderVoicesToAmpOut(0, _samples);
	renderOutput(_outputs[0], _outputs[1], _samples);

	cleanupVoices();
}

bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>& /*_response*/)
{
	if (!_ev.sysex.empty())
		return true; // No sysex support for OpenWurli

	// System real-time: ignore
	if (_ev.a >= 0xF8)
		return true;

	const uint8_t status = _ev.a & 0xF0;

	switch (status)
	{
	case 0x80: // Note Off
		noteOff(_ev.b);
		return true;

	case 0x90: // Note On
		if (_ev.c == 0)
			noteOff(_ev.b);
		else
			noteOn(_ev.b, _ev.c);
		return true;

	case 0xB0: // Control Change
		switch (_ev.b)
		{
		case 1: // Mod wheel → tremolo depth
			setTremoloDepth(static_cast<float>(_ev.c) / 127.0f);
			break;
		case 7: // Volume
			setVolume(static_cast<float>(_ev.c) / 127.0f);
			break;
		case 11: // Expression, scales the volume
			m_expression = static_cast<float>(_ev.c) / 127.0f;
			break;
		case 64: // Sustain pedal
			m_sustainPedal = (_ev.c >= 64);
			if (!m_sustainPedal)
			{
				// Lifting the pedal triggers the deferred note-off of every Sustained voice.
				for (auto& slot : m_voices)
				{
					if (slot.state == VoiceState::Sustained)
					{
						slot.state = VoiceState::Releasing;
						slot.voice.noteOff();
					}
				}
			}
			break;
		case 70: // Sound Controller 1 → MLP corrections on/off
			setMlpEnabled(_ev.c == 1 || _ev.c >= 64);	// 0/1 from the switch parameter
			break;
		case 71: // Sound Controller 2 → speaker character
			setSpeakerCharacter(static_cast<float>(_ev.c) / 127.0f);
			break;
		case 75: // Sound Controller 6 → velocity curve, 0-4 direct or 0-127 scaled
			setVelocityCurve(_ev.c < 5 ? _ev.c : _ev.c / 26);
			break;
		case 123: // All notes off
			allNotesOff();
			break;
		}
		return true;

	case 0xE0: // Pitch bend, ignored for Wurlitzer
		return true;

	default:
		break;
	}
	return true;
}

void Device::readMidiOut(std::vector<synthLib::SMidiEvent>& /*_midiOut*/)
{
	// No MIDI output from OpenWurli
}

} // namespace openWurliLib
