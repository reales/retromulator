/**
 * OpenB3 Device adapter for Retromulator
 * Wraps the Beatrix tonewheel organ and Leslie engine (OpenB3 / setBfree) as a synthLib::Device
 */
#include "device.h"

#include "BinaryData.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>
#include <cmath>
#include <cstring>

// last: the Beatrix C headers define short macros (MIN, TRUE, ...)
#include "beatrix.hpp"

namespace openB3Lib
{

namespace
{
	constexpr double kSampleRate = 48000.0;
	constexpr int kChunk = 512;
	constexpr uint32_t kStateMagic = 0x4f423353; // 'OB3S'
	constexpr uint32_t kFxpId = 0x526d4233;      // 'RmB3'

	// steps: 0 continuous, 2 switch, otherwise the number of positions (CC 0-127 split into zones)
	struct ParamDef
	{
		const char* id;
		uint8_t cc;
		int steps;
		float lo, hi, def;
		float centre;   // skew centre for continuous ranges, 0 = linear
		float interval;
	};

	constexpr ParamDef kParams[ParamCount] =
	{
		{ "DRAWBAR_UPPER_0", 12, 9, 0, 8, 8 }, { "DRAWBAR_UPPER_1", 13, 9, 0, 8, 8 },
		{ "DRAWBAR_UPPER_2", 14, 9, 0, 8, 6 }, { "DRAWBAR_UPPER_3", 15, 9, 0, 8, 0 },
		{ "DRAWBAR_UPPER_4", 16, 9, 0, 8, 0 }, { "DRAWBAR_UPPER_5", 17, 9, 0, 8, 0 },
		{ "DRAWBAR_UPPER_6", 18, 9, 0, 8, 0 }, { "DRAWBAR_UPPER_7", 19, 9, 0, 8, 0 },
		{ "DRAWBAR_UPPER_8", 20, 9, 0, 8, 0 },
		{ "DRAWBAR_LOWER_0", 21, 9, 0, 8, 8 }, { "DRAWBAR_LOWER_1", 22, 9, 0, 8, 8 },
		{ "DRAWBAR_LOWER_2", 23, 9, 0, 8, 8 }, { "DRAWBAR_LOWER_3", 24, 9, 0, 8, 8 },
		{ "DRAWBAR_LOWER_4", 25, 9, 0, 8, 0 }, { "DRAWBAR_LOWER_5", 26, 9, 0, 8, 0 },
		{ "DRAWBAR_LOWER_6", 27, 9, 0, 8, 0 }, { "DRAWBAR_LOWER_7", 28, 9, 0, 8, 0 },
		{ "DRAWBAR_LOWER_8", 29, 9, 0, 8, 8 },
		{ "DRAWBAR_PEDALBOARD_0", 30, 9, 0, 8, 8 }, { "DRAWBAR_PEDALBOARD_1", 31, 9, 0, 8, 0 },
		{ "VIBRATO_UPPER",  80, 2, 0, 1, 0 },
		{ "VIBRATO_LOWER",  81, 2, 0, 1, 0 },
		{ "VIBRATO_CHORUS", 82, 6, 0, 5, 0 },
		{ "PERC_ON_OFF",    83, 2, 0, 1, 0 },
		{ "PERC_SOFT_NORM", 84, 2, 0, 1, 0 },
		{ "PERC_FAST_SLOW", 85, 2, 0, 1, 0 },
		{ "PERC_2ND_3RD",   86, 2, 0, 1, 0 },
		{ "OVERDRIVE",      87, 2, 0, 1, 0 },
		{ "GAIN",           88, 0, 0, 1, 0.31f },
		{ "ROTARY",         89, 3, 0, 2, 0 },
		{ "LESLIE_CABINET", 102, 3, 0, 2, 0 },
		{ "LESLIE_BYPASS",  103, 2, 0, 1, 0 },
		{ "HORN_SPEED",     104, 0, 0.5f, 1.5f, 1 },
		{ "DRUM_SPEED",     105, 0, 0.5f, 1.5f, 1 },
		{ "HORN_RAMP",      106, 0, 0.25f, 4, 1, 1 },
		{ "DRUM_RAMP",      107, 0, 0.25f, 4, 1, 1 },
		{ "LESLIE_BALANCE", 108, 0, -1, 1, 0 },
		{ "MIC_DISTANCE",   109, 0, 25, 100, 42, 0, 1 },
		{ "LESLIE_WIDTH",   110, 0, 0, 1.5f, 1 },
		{ "LESLIE_BRAKE",   111, 3, 0, 2, 0 },
		{ "CABINET_BODY",   112, 0, 0, 1, 0.5f },
		{ "REVERB",          91, 0, 0, 1, 0.2f },
		{ "VOLUME",           7, 0, 0, 1, 0.75f },
	};

	juce::NormalisableRange<float> rangeOf(const ParamDef& _d)
	{
		juce::NormalisableRange<float> r(_d.lo, _d.hi, _d.interval);
		if(_d.centre > 0.0f)
			r.setSkewForCentre(_d.centre);
		return r;
	}

	float ccToValue(const ParamDef& _d, int _cc)
	{
		if(_d.steps == 0)
			return rangeOf(_d).convertFrom0to1(static_cast<float>(_cc) / 127.0f);
		if(_d.steps == 2)
			return _cc >= 64 ? 1.0f : 0.0f;
		return static_cast<float>(_cc * _d.steps / 128);
	}

	uint8_t valueToCc(const ParamDef& _d, float _v)
	{
		if(_d.steps == 0)
			return static_cast<uint8_t>(std::lround(rangeOf(_d).convertTo0to1(juce::jlimit(_d.lo, _d.hi, _v)) * 127.0f));
		if(_d.steps == 2)
			return _v >= 0.5f ? 127 : 0;
		const int idx = juce::jlimit(0, _d.steps - 1, static_cast<int>(std::lround(_v)));
		return static_cast<uint8_t>(std::min(127, (idx * 128 + 64) / _d.steps));
	}

	int paramForCc(uint8_t _cc)
	{
		for(int i = 0; i < ParamCount; ++i)
			if(kParams[i].cc == _cc)
				return i;
		return -1;
	}

	bool isLeslieParam(int _p)
	{
		return _p == Cabinet || _p == LeslieBypass || (_p >= HornSpeed && _p <= Brake);
	}

	// Built-in presets from OpenB3. Parameters not listed keep their defaults.
	struct Preset
	{
		const char* name;
		const char* upper;
		const char* lower;
		bool vibUpper, vibLower;
		int vibType;            // 0 V1, 1 C1, 2 V2, 3 C2, 4 V3, 5 C3
		bool perc, percSoft, percFast, perc2nd;
		bool overdrive;
		float gain;
		int rotary;             // 0 slow, 1 stop, 2 fast
		float reverb;
	};

	const Preset kPresets[] =
	{
		{ "Jazz Perc",       "888000000", "838000000", false, false, 5, true,  false, true,  false, false, 0.10f, 0, 0.15f },
		{ "Soul Comp",       "888000000", "808000000", true,  true,  5, true,  true,  false, false, true,  0.45f, 0, 0.20f },
		{ "Gospel",          "888800000", "008800000", true,  true,  5, false, false, false, false, true,  0.60f, 2, 0.20f },
		{ "Full Organ",      "888888888", "888800000", true,  true,  5, false, false, false, false, true,  0.70f, 2, 0.25f },
		{ "Rock Overdrive",  "888400000", "838000000", true,  false, 5, false, false, false, false, true,  1.00f, 0, 0.15f },
		{ "Screaming Lead",  "800000888", "808000000", false, false, 5, false, false, false, false, true,  0.90f, 2, 0.15f },
		{ "Blues Mellow",    "838000000", "008500000", true,  true,  5, false, false, false, false, false, 0.10f, 0, 0.25f },
		{ "Ballad",          "006800000", "005400000", true,  true,  3, false, false, false, false, false, 0.10f, 0, 0.35f },
		{ "Flutes",          "008000000", "006000000", true,  true,  2, false, false, false, false, false, 0.10f, 0, 0.30f },
		{ "Percussive Pop",  "800000000", "808000000", false, false, 5, true,  false, true,  false, false, 0.10f, 1, 0.10f },
	};

	std::array<float, ParamCount> presetValues(const Preset& _p)
	{
		std::array<float, ParamCount> v{};
		for(int i = 0; i < ParamCount; ++i)
			v[i] = kParams[i].def;
		for(int i = 0; i < 9; ++i)
		{
			v[DrawbarUpper1 + i] = static_cast<float>(_p.upper[i] - '0');
			v[DrawbarLower1 + i] = static_cast<float>(_p.lower[i] - '0');
		}
		v[VibratoUpper] = _p.vibUpper ? 1.0f : 0.0f;
		v[VibratoLower] = _p.vibLower ? 1.0f : 0.0f;
		v[VibratoType]  = static_cast<float>(_p.vibType);
		v[PercOn]       = _p.perc ? 1.0f : 0.0f;
		v[PercSoft]     = _p.percSoft ? 1.0f : 0.0f;
		v[PercFast]     = _p.percFast ? 1.0f : 0.0f;
		v[PercHarmonic] = _p.perc2nd ? 1.0f : 0.0f;
		v[Overdrive]    = _p.overdrive ? 1.0f : 0.0f;
		v[Drive]        = _p.gain;
		v[Rotary]       = static_cast<float>(_p.rotary);
		v[Reverb]       = _p.reverb;
		return v;
	}

	void writeBE32(juce::OutputStream& _out, uint32_t _v)
	{
		_out.writeIntBigEndian(static_cast<int>(_v));
	}

	bool writeFxp(const juce::File& _file, const std::string& _name, const std::array<float, ParamCount>& _values)
	{
		juce::XmlElement xml("OpenB3");
		for(int i = 0; i < ParamCount; ++i)
		{
			auto* e = xml.createNewChildElement("PARAM");
			e->setAttribute("id", kParams[i].id);
			e->setAttribute("value", static_cast<double>(_values[i]));
		}

		juce::MemoryBlock chunk;
		juce::AudioProcessor::copyXmlToBinary(xml, chunk);

		char prgName[28] = {};
		std::strncpy(prgName, _name.c_str(), sizeof(prgName) - 1);

		juce::MemoryOutputStream out;
		writeBE32(out, 0x43636e4b);    // 'CcnK'
		writeBE32(out, static_cast<uint32_t>(52 + chunk.getSize()));
		writeBE32(out, 0x46504368);    // 'FPCh'
		writeBE32(out, 1);             // format version
		writeBE32(out, kFxpId);
		writeBE32(out, 1);             // fx version
		writeBE32(out, 1);             // programs
		out.write(prgName, sizeof(prgName));
		writeBE32(out, static_cast<uint32_t>(chunk.getSize()));
		out.write(chunk.getData(), chunk.getSize());

		_file.getParentDirectory().createDirectory();
		return _file.replaceWithData(out.getData(), out.getDataSize());
	}
}

Device::Device(const synthLib::DeviceCreateParams& _params)
	: synthLib::Device(_params)
{
	for(int i = 0; i < ParamCount; ++i)
		m_values[i].store(valueToCc(kParams[i], kParams[i].def));

	m_beatrix = std::make_unique<Beatrix>(kSampleRate);
	m_beatrix->preamp_hook = [this](float* in, float* out, int n) { processPreamp(in, out, n); };
	m_beatrix->use_internal_reverb = false;

	m_oversampler.initProcessing(BUFFER_SIZE_SAMPLES);
	m_oversampler.reset();
	m_odScratch.assign(BUFFER_SIZE_SAMPLES, 0.0f);

	// pre FIR (~4) + decimation FIR (16) at host rate, plus the IIR oversampler
	m_overdriveLatency = 20 + static_cast<int>(std::round(m_oversampler.getLatencyInSamples()));
	m_cleanDelay.assign(static_cast<size_t>(m_overdriveLatency), 0.0f);

	const juce::dsp::ProcessSpec spec { kSampleRate, static_cast<juce::uint32>(kChunk), 2 };

	static const char* const cabNames[] = { "openb3_cab_122_wav", "openb3_cab_145_wav", "openb3_cab_16_wav" };
	for(size_t i = 0; i < m_cabinets.size(); ++i)
	{
		m_cabinets[i] = std::make_unique<juce::dsp::Convolution>(juce::dsp::Convolution::Latency { 0 }, m_convQueue);
		int size = 0;
		if(const auto* data = BinaryData::getNamedResource(cabNames[i], size))
			m_cabinets[i]->loadImpulseResponse(data, static_cast<size_t>(size), juce::dsp::Convolution::Stereo::yes,
			                                   juce::dsp::Convolution::Trim::no, 0, juce::dsp::Convolution::Normalise::no);
		m_cabinets[i]->prepare(spec);
	}

	m_plate = std::make_unique<juce::dsp::Convolution>(juce::dsp::Convolution::NonUniform { 256 }, m_convQueue);
	m_plate->loadImpulseResponse(BinaryData::openb3_plate_ir_wav, BinaryData::openb3_plate_ir_wavSize,
	                             juce::dsp::Convolution::Stereo::yes, juce::dsp::Convolution::Trim::no, 0,
	                             juce::dsp::Convolution::Normalise::yes);
	m_plate->prepare(spec);

	m_bodyBuffer.setSize(2, kChunk);
	m_wetBuffer.setSize(2, kChunk);

	m_bodyMix.reset(kSampleRate, 0.05);
	m_width.reset(kSampleRate, 0.05);
	m_reverbMix.reset(kSampleRate, 0.05);

	applyParams(true);
	m_bodyMix.setCurrentAndTargetValue(m_bodyMix.getTargetValue());
	m_width.setCurrentAndTargetValue(m_width.getTargetValue());
	m_reverbMix.setCurrentAndTargetValue(m_reverbMix.getTargetValue());
}

Device::~Device() = default;

float Device::getSamplerate() const
{
	return static_cast<float>(kSampleRate);
}

uint64_t Device::getDspClockHz() const
{
	return static_cast<uint64_t>(kSampleRate);
}

void Device::applyParams(const bool _force)
{
	std::array<bool, ParamCount> changed{};
	bool any = _force;
	for(int i = 0; i < ParamCount; ++i)
	{
		const int v = m_values[i].load(std::memory_order_relaxed);
		changed[i] = _force || v != m_applied[i];
		m_applied[i] = v;
		any |= changed[i];
	}

	if(!any && m_expression == m_appliedExpression)
		return;

	auto val = [this](int _p) { return ccToValue(kParams[_p], m_applied[_p]); };
	auto on = [&](int _p) { return val(_p) > 0.5f; };
	auto& b = *m_beatrix;

	auto anyChanged = [&](int _first, int _count)
	{
		for(int i = _first; i < _first + _count; ++i)
			if(changed[i])
				return true;
		return false;
	};

	if(anyChanged(DrawbarUpper1, 9) || anyChanged(DrawbarLower1, 9))
	{
		unsigned int upper[9], lower[9];
		for(int i = 0; i < 9; ++i)
		{
			upper[i] = static_cast<unsigned int>(val(DrawbarUpper1 + i));
			lower[i] = static_cast<unsigned int>(val(DrawbarLower1 + i));
		}
		b.set_drawbars(UPPER_MANUAL, upper);
		b.set_drawbars(LOWER_MANUAL, lower);
	}

	if(anyChanged(DrawbarPedal1, 2))
	{
		// pedal bus: 16' and 8' sit at the first and third footage
		unsigned int pedal[9] = {};
		pedal[0] = static_cast<unsigned int>(val(DrawbarPedal1));
		pedal[2] = static_cast<unsigned int>(val(DrawbarPedal2));
		b.set_drawbars(PEDAL_BOARD, pedal);
	}

	if(changed[VibratoUpper]) b.set_vibrato_upper(on(VibratoUpper));
	if(changed[VibratoLower]) b.set_vibrato_lower(on(VibratoLower));
	if(changed[VibratoType])
	{
		static const int types[] = { VIB1, CHO1, VIB2, CHO2, VIB3, CHO3 };
		b.set_vibrato(types[juce::jlimit(0, 5, static_cast<int>(val(VibratoType)))]);
	}

	if(changed[PercOn])       b.set_percussion_enabled(on(PercOn));
	if(changed[PercSoft])     b.set_percussion_volume(on(PercSoft));
	if(changed[PercFast])     b.set_percussion_fast(on(PercFast));
	if(changed[PercHarmonic]) b.set_percussion_first(on(PercHarmonic));

	if(changed[Overdrive]) b.set_preamp_clean(!on(Overdrive));
	if(changed[Drive])     b.set_input_gain(val(Drive));

	bool leslie = false;
	for(int i = 0; i < ParamCount; ++i)
		leslie |= changed[i] && isLeslieParam(i);
	if(leslie)
	{
		whirl_settings s {};
		s.model     = static_cast<int>(val(Cabinet));
		s.hornSpeed = val(HornSpeed);
		s.drumSpeed = val(DrumSpeed);
		s.hornRamp  = val(HornRamp);
		s.drumRamp  = val(DrumRamp);
		s.balance   = val(Balance);
		s.micCm     = val(MicDistance);
		s.brake     = static_cast<int>(val(Brake));
		s.bypass    = on(LeslieBypass) ? 1 : 0;
		b.apply_leslie(s);
	}

	// index order matches WHIRL_SLOW, WHIRL_STOP, WHIRL_FAST
	if(changed[Rotary])
		b.set_rotary_speed(static_cast<int>(val(Rotary)));

	if(changed[Cabinet])
	{
		const int cab = juce::jlimit(0, 2, static_cast<int>(val(Cabinet)));
		if(cab != m_activeCabinet)
		{
			m_cabinets[static_cast<size_t>(cab)]->reset();
			m_activeCabinet = cab;
		}
	}

	if(changed[CabinetBody]) m_bodyMix.setTargetValue(val(CabinetBody));
	if(changed[Width])       m_width.setTargetValue(val(Width));
	if(changed[Reverb])      m_reverbMix.setTargetValue(val(Reverb));

	if(changed[Volume] || m_expression != m_appliedExpression)
		b.set_swell(val(Volume) * m_expression);
	m_appliedExpression = m_expression;
}

void Device::processPreamp(float* _in, float* _out, const int _n)
{
	void* pa = m_beatrix->inst.preamp;

	if(isPreampClean(pa))
	{
		for(int i = 0; i < _n; ++i)
		{
			_out[i] = m_cleanDelay[static_cast<size_t>(m_cleanDelayPos)];
			m_cleanDelay[static_cast<size_t>(m_cleanDelayPos)] = _in[i];
			m_cleanDelayPos = (m_cleanDelayPos + 1) % m_overdriveLatency;
		}
		return;
	}

	overdrivePre(pa, _in, m_odScratch.data(), static_cast<size_t>(_n));

	float* channels[] = { m_odScratch.data() };
	juce::dsp::AudioBlock<float> block(channels, 1, static_cast<size_t>(_n));
	auto up = m_oversampler.processSamplesUp(block);
	overdriveShape(pa, up.getChannelPointer(0), up.getNumSamples(), static_cast<int>(m_oversampler.getOversamplingFactor()));
	m_oversampler.processSamplesDown(block);

	overdrivePost(pa, m_odScratch.data(), _out, static_cast<size_t>(_n));
}

void Device::renderChunk(float* _l, float* _r, const int _n)
{
	m_beatrix->get_next_block(_l, _r, _n);

	// cabinet body: blend of the rotors and the rotors through the cabinet IR
	m_bodyBuffer.copyFrom(0, 0, _l, _n);
	m_bodyBuffer.copyFrom(1, 0, _r, _n);
	juce::dsp::AudioBlock<float> bodyBlock(m_bodyBuffer.getArrayOfWritePointers(), 2, static_cast<size_t>(_n));
	m_cabinets[static_cast<size_t>(m_activeCabinet)]->process(juce::dsp::ProcessContextReplacing<float>(bodyBlock));

	for(int i = 0; i < _n; ++i)
	{
		const float b = m_bodyMix.getNextValue();
		const float l = _l[i] + b * (m_bodyBuffer.getSample(0, i) - _l[i]);
		const float r = _r[i] + b * (m_bodyBuffer.getSample(1, i) - _r[i]);

		// mid/side width: 0 mono, 1 as miked, 1.5 wider
		const float mid = 0.5f * (l + r);
		const float side = 0.5f * (l - r) * m_width.getNextValue();
		_l[i] = mid + side;
		_r[i] = mid - side;
	}

	m_wetBuffer.copyFrom(0, 0, _l, _n);
	m_wetBuffer.copyFrom(1, 0, _r, _n);
	juce::dsp::AudioBlock<float> wetBlock(m_wetBuffer.getArrayOfWritePointers(), 2, static_cast<size_t>(_n));
	m_plate->process(juce::dsp::ProcessContextReplacing<float>(wetBlock));

	for(int i = 0; i < _n; ++i)
	{
		const float mix = m_reverbMix.getNextValue();
		_l[i] += mix * m_wetBuffer.getSample(0, i);
		_r[i] += mix * m_wetBuffer.getSample(1, i);
	}
}

void Device::processAudio(const synthLib::TAudioInputs&, const synthLib::TAudioOutputs& _outputs, const size_t _samples)
{
	juce::ScopedNoDenormals noDenormals;

	applyParams(false);

	float* l = _outputs[0];
	float* r = _outputs[1];
	size_t done = 0;
	while(done < _samples)
	{
		const int n = static_cast<int>(std::min<size_t>(kChunk, _samples - done));
		renderChunk(l + done, r + done, n);
		done += static_cast<size_t>(n);
	}
}

uint8_t Device::rotaryCc()
{
	return kParams[Rotary].cc;
}

int Device::rotaryCcFromPedal(uint8_t _cc, uint8_t _value, uint8_t _currentRotaryCc)
{
	const auto& d = kParams[Rotary];
	if(_cc == 1)        // mod wheel: slow / fast
		return valueToCc(d, _value < 64 ? WHIRL_SLOW : WHIRL_FAST);
	if(_cc == 64 && _value > 63)    // sustain pedal press toggles slow / fast
		return valueToCc(d, static_cast<int>(ccToValue(d, _currentRotaryCc)) == WHIRL_FAST ? WHIRL_SLOW : WHIRL_FAST);
	return -1;
}

bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>&)
{
	if(!_ev.sysex.empty() || _ev.a >= 0xf0)
		return true;

	const uint8_t status = _ev.a & 0xf0;

	if(status == 0x80 || status == 0x90)
	{
		// channel 1 upper manual, 2 lower manual, 3 pedals
		uint8_t msg[3] = { _ev.a, _ev.b, _ev.c };
		if(status == 0x90 && _ev.c == 0)
			msg[0] = static_cast<uint8_t>(0x80 | (_ev.a & 0x0f));

		// toggling the split re-routes keys, so release everything that was held
		const bool split = m_split.load();
		if(split != m_appliedSplit)
		{
			midi_panic(&m_beatrix->inst);
			m_appliedSplit = split;
		}
		if(split && (_ev.a & 0x0f) == 0)
		{
			const uint8_t channel = _ev.b < 36 ? 2 : _ev.b < 60 ? 1 : 0;
			msg[0] = static_cast<uint8_t>((msg[0] & 0xf0) | channel);
		}
		m_beatrix->process_midi_message(msg, 3);
		return true;
	}

	if(status == 0xc0)
	{
		m_pendingProgram.store(_ev.b);
		return true;
	}

	if(status != 0xb0)
		return true;

	switch(_ev.b)
	{
	case 1:
	case 64:
	{
		const int cc = rotaryCcFromPedal(_ev.b, _ev.c, m_values[Rotary].load());
		if(cc >= 0)
			setParamCc(Rotary, static_cast<uint8_t>(cc));
		return true;
	}
	case 11:
		m_expression = static_cast<float>(_ev.c) / 127.0f;
		return true;
	case 120:
	case 123:
		midi_panic(&m_beatrix->inst);
		return true;
	default:
		break;
	}

	const int p = paramForCc(_ev.b);
	if(p < 0)
		return true;

	// switch slots send 0/1, hardware sends 0-127
	if(kParams[p].steps == 2)
		setParamCc(static_cast<Param>(p), (_ev.c == 1 || _ev.c >= 64) ? 127 : 0);
	else
		setParamCc(static_cast<Param>(p), _ev.c);
	return true;
}

void Device::readMidiOut(std::vector<synthLib::SMidiEvent>&)
{
}

std::vector<std::pair<uint8_t, uint8_t>> Device::getCcValues() const
{
	std::vector<std::pair<uint8_t, uint8_t>> out;
	out.reserve(ParamCount);
	for(int i = 0; i < ParamCount; ++i)
		out.emplace_back(kParams[i].cc, m_values[i].load(std::memory_order_relaxed));
	return out;
}

bool Device::loadPreset(const std::string& _path)
{
	const juce::File file(juce::String::fromUTF8(_path.c_str()));
	juce::MemoryBlock data;
	if(!file.loadFileAsData(data) || data.getSize() < 60)
		return false;

	juce::MemoryInputStream in(data, false);
	const auto readBE = [&in] { return static_cast<uint32_t>(in.readIntBigEndian()); };

	if(readBE() != 0x43636e4b)     // 'CcnK'
		return false;
	readBE();
	if(readBE() != 0x46504368)     // 'FPCh'
		return false;
	in.skipNextBytes(12);          // version, fx id, fx version
	readBE();                      // programs
	char prgName[29] = {};
	in.read(prgName, 28);
	const auto chunkSize = static_cast<int>(readBE());
	if(chunkSize <= 0 || in.getPosition() + chunkSize > static_cast<juce::int64>(data.getSize()))
		return false;

	const auto* chunk = static_cast<const char*>(data.getData()) + in.getPosition();
	auto xml = juce::AudioProcessor::getXmlFromBinary(chunk, chunkSize);
	if(!xml)
		xml = juce::parseXML(juce::String::fromUTF8(chunk, chunkSize));
	if(!xml)
		return false;

	std::array<float, ParamCount> values{};
	for(int i = 0; i < ParamCount; ++i)
		values[i] = kParams[i].def;
	for(const auto* e : xml->getChildWithTagNameIterator("PARAM"))
	{
		const auto id = e->getStringAttribute("id");
		for(int i = 0; i < ParamCount; ++i)
			if(id == kParams[i].id)
				values[i] = static_cast<float>(e->getDoubleAttribute("value", kParams[i].def));
	}

	for(int i = 0; i < ParamCount; ++i)
		setParamCc(static_cast<Param>(i), valueToCc(kParams[i], values[i]));

	m_patchName = prgName[0] ? std::string(prgName) : file.getFileNameWithoutExtension().toStdString();
	return true;
}

bool Device::savePreset(const std::string& _path, const std::string& _name) const
{
	std::array<float, ParamCount> values{};
	for(int i = 0; i < ParamCount; ++i)
		values[i] = ccToValue(kParams[i], m_values[i].load(std::memory_order_relaxed));
	return writeFxp(juce::File(juce::String::fromUTF8(_path.c_str())), _name, values);
}

void Device::writeFactoryPresets(const std::string& _folder)
{
	const juce::File dir(juce::String::fromUTF8(_folder.c_str()));
	dir.createDirectory();

	int index = 1;
	for(const auto& p : kPresets)
	{
		const auto file = dir.getChildFile(juce::String(index++).paddedLeft('0', 2) + " " + p.name + ".fxp");
		if(!file.existsAsFile())
			writeFxp(file, p.name, presetValues(p));
	}
}

#if SYNTHLIB_DEMO_MODE == 0
bool Device::getState(std::vector<uint8_t>& _state, const synthLib::StateType _type)
{
	if(_type != synthLib::StateTypeGlobal)
		return false;

	_state.resize(8 + ParamCount + 1);
	std::memcpy(_state.data(), &kStateMagic, 4);
	const uint32_t count = ParamCount;
	std::memcpy(_state.data() + 4, &count, 4);
	for(int i = 0; i < ParamCount; ++i)
		_state[8 + i] = m_values[i].load(std::memory_order_relaxed);
	_state[8 + ParamCount] = m_split.load() ? 1 : 0;
	return true;
}

bool Device::setState(const std::vector<uint8_t>& _state, const synthLib::StateType _type)
{
	if(_type != synthLib::StateTypeGlobal || _state.size() < 8)
		return false;

	uint32_t magic = 0, count = 0;
	std::memcpy(&magic, _state.data(), 4);
	std::memcpy(&count, _state.data() + 4, 4);
	if(magic != kStateMagic)
		return false;

	// parameters added later keep their defaults when loading an older state
	const auto n = std::min<size_t>({ count, ParamCount, _state.size() - 8 });
	for(size_t i = 0; i < n; ++i)
		setParamCc(static_cast<Param>(i), _state[8 + i]);
	if(_state.size() > 8 + count)
		m_split.store(_state[8 + count] != 0);
	return true;
}
#endif

} // namespace openB3Lib
