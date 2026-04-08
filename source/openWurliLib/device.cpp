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

Device::Device(const synthLib::DeviceCreateParams& _params)
	: synthLib::Device(_params)
{
	m_sampleRate = 44100.0;
	m_oversample = m_sampleRate < 88200.0;
	m_osSampleRate = m_oversample ? m_sampleRate * 2.0 : m_sampleRate;

	m_preamp.init(m_osSampleRate);
	m_tremolo.init(m_tremoloDepth, m_osSampleRate);
	m_oversampler.init();
	m_speaker.init(m_sampleRate);

	m_voiceBuf.resize(MAX_BLOCK, 0.0);
	m_sumBuf.resize(MAX_BLOCK, 0.0);
	m_upBuf.resize(MAX_BLOCK * 2, 0.0);
	m_outBuf.resize(MAX_BLOCK, 0.0);

	std::memset(m_sustainedNotes, 0, sizeof(m_sustainedNotes));
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
	return true; // No ROM needed — pure physical model
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
		// velocityCurve added later — backwards compatible (missing = default)
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
	case 3: vel = std::sqrt(rawVel);                          break; // Hard (sqrt — boosted low velocities)
	case 4: vel = 0.75;                                       break; // Fixed (mezzo-forte)
	default: vel = rawVel;                                    break;
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
	if (m_sustainPedal)
	{
		m_sustainedNotes[note & 0x7F]++;
		return;
	}

	// Release oldest held voice matching this note
	size_t bestIdx = MAX_VOICES;
	uint64_t bestAge = UINT64_MAX;

	for (size_t i = 0; i < MAX_VOICES; i++)
	{
		if (m_voices[i].state == VoiceState::Held && m_voices[i].midiNote == note)
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
		m_voices[bestIdx].state = VoiceState::Releasing;
		m_voices[bestIdx].voice.noteOff();
	}
}

void Device::allNotesOff()
{
	m_sustainPedal = false;
	std::memset(m_sustainedNotes, 0, sizeof(m_sustainedNotes));

	for (auto& slot : m_voices)
	{
		if (slot.state == VoiceState::Held)
		{
			slot.state = VoiceState::Releasing;
			slot.voice.noteOff();
		}
	}
}

size_t Device::allocateVoice()
{
	size_t bestIdx = 0;
	uint64_t bestPriority = UINT64_MAX;

	for (size_t i = 0; i < MAX_VOICES; i++)
	{
		if (m_voices[i].state == VoiceState::Free)
			return i;

		const uint64_t priority = (m_voices[i].state == VoiceState::Releasing)
			? m_voices[i].age
			: m_voices[i].age + UINT64_MAX / 2;

		if (priority < bestPriority)
		{
			bestPriority = priority;
			bestIdx = i;
		}
	}
	return bestIdx;
}

void Device::renderSubblock(size_t offset, size_t len)
{
	// Sum all active voices
	std::fill(m_sumBuf.begin(), m_sumBuf.begin() + len, 0.0);

	for (auto& slot : m_voices)
	{
		if (slot.state == VoiceState::Free && !slot.hasStealVoice)
			continue;

		// Render main voice
		if (slot.state != VoiceState::Free)
		{
			slot.voice.render(m_voiceBuf.data(), len);
			for (size_t i = 0; i < len; i++)
				m_sumBuf[i] += m_voiceBuf[i];
		}

		// Render stealing voice with fade-out
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

	// NaN guard on voice output
	bool hasNan = false;
	for (size_t i = 0; i < len; i++)
	{
		if (!std::isfinite(m_sumBuf[i])) { hasNan = true; break; }
	}
	if (hasNan)
		std::fill(m_sumBuf.begin(), m_sumBuf.begin() + len, 0.0);

	// Preamp processing
	if (m_oversample)
	{
		m_oversampler.upsample2x(m_sumBuf.data(), m_upBuf.data(), len);

		for (size_t i = 0; i < len; i++)
		{
			m_tremolo.setDepth(static_cast<double>(m_tremoloDepth));

			for (int j = 0; j < 2; j++)
			{
				const size_t idx = i * 2 + j;
				const double rLdr = m_tremolo.process();
				m_preamp.setLdrResistance(rLdr);
				m_upBuf[idx] = m_preamp.processSample(m_upBuf[idx]);
			}
		}

		m_oversampler.downsample2x(m_upBuf.data(), &m_outBuf[offset], len);
	}
	else
	{
		for (size_t i = 0; i < len; i++)
		{
			const double rLdr = m_tremolo.process();
			m_preamp.setLdrResistance(rLdr);
			m_outBuf[offset + i] = m_preamp.processSample(m_sumBuf[i]);
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

	// Ensure buffers are large enough
	if (m_outBuf.size() < _samples)
	{
		m_voiceBuf.resize(_samples, 0.0);
		m_sumBuf.resize(_samples, 0.0);
		m_upBuf.resize(_samples * 2, 0.0);
		m_outBuf.resize(_samples, 0.0);
	}

	// Render all voices in one block (MIDI events already dispatched by base class)
	renderSubblock(0, _samples);

	// Output chain: volume → power amp → speaker
	auto* outL = _outputs[0];
	auto* outR = _outputs[1];

	for (size_t i = 0; i < _samples; i++)
	{
		const double volume = static_cast<double>(m_volume);
		m_speaker.setCharacter(static_cast<double>(m_speakerCharacter));

		// Volume pot (audio taper: vol²)
		const double attenuated = m_outBuf[i] * volume * volume;
		const double amplified = m_powerAmp.process(attenuated);
		const double shaped = m_speaker.process(amplified);
		const double post = shaped * openWurli::POST_SPEAKER_GAIN;

		float sample = static_cast<float>(post);
		if (!std::isfinite(sample))
		{
			m_preamp.reset();
			m_oversampler.reset();
			m_powerAmp.reset();
			m_speaker.reset();
			sample = 0.0f;
		}

		outL[i] = sample;
		outR[i] = sample; // mono → stereo
	}

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
			m_tremoloDepth = static_cast<float>(_ev.c) / 127.0f;
			break;
		case 7: // Volume
			m_volume = static_cast<float>(_ev.c) / 127.0f;
			break;
		case 11: // Expression
			m_volume = static_cast<float>(_ev.c) / 127.0f;
			break;
		case 64: // Sustain pedal
			m_sustainPedal = (_ev.c >= 64);
			if (!m_sustainPedal)
			{
				// Release sustained notes (one noteOff per deferred note-off)
				for (int n = 0; n < 128; n++)
				{
					while (m_sustainedNotes[n] > 0)
					{
						m_sustainedNotes[n]--;
						noteOff(static_cast<uint8_t>(n));
					}
				}
			}
			break;
		case 71: // Sound Controller 2 → speaker character
			m_speakerCharacter = static_cast<float>(_ev.c) / 127.0f;
			break;
		case 123: // All notes off
			allNotesOff();
			break;
		}
		return true;

	case 0xE0: // Pitch bend — ignored for Wurlitzer
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
