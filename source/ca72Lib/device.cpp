/**
 * CA-72 Device adapter for Retromulator
 * Wraps the CA-72 Minimoog Model D circuit model as a synthLib::Device
 */
#include "device.h"
#include "synth.h"
#include "Presets.h"

#include <cstring>

namespace ca72Lib
{
namespace
{
	constexpr uint32_t kFxpId = 0x43413732;   // 'CA72'
	constexpr int kMaxBlock = 8192;

	// Parameter -> MIDI CC. bipolar: CC 64 is the centre of the range.
	struct CcDef
	{
		const char* id;
		uint8_t cc;
		bool bipolar = false;
	};

	const CcDef kCcs[] =
	{
		{ "tune", 14, true }, { "glide", 5 }, { "glideOn", 65 }, { "decay", 15 },
		{ "modMix", 16 }, { "oscMod", 17 }, { "filterMod", 18 }, { "osc3Control", 19 },
		{ "range1", 20 }, { "wave1", 21 }, { "range2", 22 }, { "wave2", 23 }, { "freq2", 24, true },
		{ "range3", 25 }, { "wave3", 26 }, { "freq3", 27, true },
		{ "on1", 28 }, { "vol1", 29 }, { "extOn", 30 }, { "extVol", 31 }, { "on2", 33 }, { "vol2", 34 },
		{ "noiseOn", 35 }, { "noiseVol", 36 }, { "on3", 37 }, { "vol3", 39 }, { "noisePink", 40 },
		{ "cutoff", 74, true }, { "emphasis", 71 }, { "contourAmt", 41 }, { "kbd1", 42 }, { "kbd2", 43 },
		{ "filterHi", 44 }, { "fAttack", 45 }, { "fDecay", 46 }, { "fSustain", 47 },
		{ "lAttack", 73 }, { "lDecay", 75 }, { "lSustain", 76 },
		{ "volume", 7 }, { "a440", 49 }, { "feedback", 50 }, { "drive", 51 }, { "level", 52 },
		{ "priority", 53 }, { "retrigger", 54 }, { "doubled", 55 }, { "detune", 56 },
		{ "spread", 57 }, { "entropy", 58 }, { "quality", 59 },
	};

	const CcDef* ccDefFor(const juce::String& _id)
	{
		for(const auto& d : kCcs)
			if(_id == d.id)
				return &d;
		return nullptr;
	}

	float ccToNormalised(const juce::RangedAudioParameter& _p, const CcDef& _d, int _cc)
	{
		if(dynamic_cast<const juce::AudioParameterBool*>(&_p))
			return (_cc == 1 || _cc >= 64) ? 1.0f : 0.0f;
		if(auto* c = dynamic_cast<const juce::AudioParameterChoice*>(&_p))
		{
			const int n = c->choices.size();
			return _p.convertTo0to1(static_cast<float>(std::min(n - 1, _cc * n / 128)));
		}
		if(_d.bipolar)
			return _cc <= 64 ? static_cast<float>(_cc) / 128.0f : 0.5f + static_cast<float>(_cc - 64) / 126.0f;
		return static_cast<float>(_cc) / 127.0f;
	}

	uint8_t normalisedToCc(const juce::RangedAudioParameter& _p, const CcDef& _d, float _norm)
	{
		if(dynamic_cast<const juce::AudioParameterBool*>(&_p))
			return _norm >= 0.5f ? 127 : 0;
		if(auto* c = dynamic_cast<const juce::AudioParameterChoice*>(&_p))
		{
			const int n = c->choices.size();
			const int idx = juce::jlimit(0, n - 1, juce::roundToInt(_p.convertFrom0to1(_norm)));
			return static_cast<uint8_t>(std::min(127, (idx * 128 + 64) / n));
		}
		if(_d.bipolar)
			return static_cast<uint8_t>(_norm <= 0.5f ? juce::roundToInt(_norm * 128.0f)
			                                          : 64 + juce::roundToInt((_norm - 0.5f) * 126.0f));
		return static_cast<uint8_t>(juce::roundToInt(juce::jlimit(0.0f, 1.0f, _norm) * 127.0f));
	}

	juce::MemoryBlock presetChunk(const std::map<juce::String, float>& _values)
	{
		juce::XmlElement xml("CA72");
		for(const auto& [id, value] : _values)
		{
			auto* e = xml.createNewChildElement("PARAM");
			e->setAttribute("id", id);
			e->setAttribute("value", static_cast<double>(value));
		}
		juce::MemoryBlock chunk;
		juce::AudioProcessor::copyXmlToBinary(xml, chunk);
		return chunk;
	}

	std::map<juce::String, float> presetValues(const void* _chunk, int _size)
	{
		std::map<juce::String, float> values;
		auto xml = juce::AudioProcessor::getXmlFromBinary(_chunk, _size);
		if(!xml)
			xml = juce::parseXML(juce::String::fromUTF8(static_cast<const char*>(_chunk), _size));
		if(!xml)
			return values;
		for(const auto* e : xml->getChildWithTagNameIterator("PARAM"))
			values[e->getStringAttribute("id")] = static_cast<float>(e->getDoubleAttribute("value"));
		return values;
	}

	void writeName(juce::OutputStream& _out, const std::string& _name)
	{
		char prgName[28] = {};
		std::strncpy(prgName, _name.c_str(), sizeof(prgName) - 1);
		_out.write(prgName, sizeof(prgName));
	}

	juce::MemoryBlock fxpData(const std::string& _name, const juce::MemoryBlock& _chunk)
	{
		juce::MemoryOutputStream out;
		out.writeIntBigEndian(0x43636e4b);    // 'CcnK'
		out.writeIntBigEndian(static_cast<int>(52 + _chunk.getSize()));
		out.writeIntBigEndian(0x46504368);    // 'FPCh'
		out.writeIntBigEndian(1);             // format version
		out.writeIntBigEndian(static_cast<int>(kFxpId));
		out.writeIntBigEndian(1);             // fx version
		out.writeIntBigEndian(1);             // parameters (unused with a chunk)
		writeName(out, _name);
		out.writeIntBigEndian(static_cast<int>(_chunk.getSize()));
		out.write(_chunk.getData(), _chunk.getSize());
		return out.getMemoryBlock();
	}

	// One 'FPCh' program at the stream's position: its name and chunk
	bool readFxp(juce::MemoryInputStream& _in, std::string& _name, juce::MemoryBlock& _chunk)
	{
		const auto start = _in.getPosition();
		if(_in.readIntBigEndian() != 0x43636e4b)   // 'CcnK'
			return false;
		const auto byteSize = static_cast<juce::int64>(static_cast<uint32_t>(_in.readIntBigEndian()));
		if(_in.readIntBigEndian() != 0x46504368)   // 'FPCh'
			return false;
		_in.skipNextBytes(16);                     // version, fx id, fx version, parameters
		char prgName[29] = {};
		_in.read(prgName, 28);
		const auto chunkSize = static_cast<juce::int64>(static_cast<uint32_t>(_in.readIntBigEndian()));
		if(chunkSize <= 0 || _in.getNumBytesRemaining() < chunkSize)
			return false;
		_chunk.setSize(static_cast<size_t>(chunkSize));
		_in.read(_chunk.getData(), static_cast<int>(chunkSize));
		_name = prgName;
		_in.setPosition(std::max(_in.getPosition(), start + 8 + byteSize));
		return true;
	}
}

Device::Device(const synthLib::DeviceCreateParams& _params)
	: synthLib::Device(_params)
	, m_synth(std::make_shared<Synth>())
{
	m_synth->setPlayConfigDetails(0, 2, m_samplerate, kMaxBlock);
	m_synth->prepareToPlay(m_samplerate, kMaxBlock);
}

Device::~Device() = default;

void Device::getSupportedSamplerates(std::vector<float>& _dst) const
{
	for(const float sr : { 44100.0f, 48000.0f, 88200.0f, 96000.0f, 176400.0f, 192000.0f })
		_dst.push_back(sr);
}

bool Device::setSamplerate(const float _samplerate)
{
	if(!synthLib::Device::setSamplerate(_samplerate))
		return false;
	m_samplerate = _samplerate;
	m_synth->setRateAndBufferSizeDetails(_samplerate, kMaxBlock);
	m_synth->prepareToPlay(_samplerate, kMaxBlock);
	return true;
}

void Device::setNonRealtime(const bool _offline)
{
	if(m_synth->isNonRealtime() != _offline)
		m_synth->setNonRealtime(_offline);
}

void Device::processAudio(const synthLib::TAudioInputs&, const synthLib::TAudioOutputs& _outputs, const size_t _samples)
{
	size_t done = 0;
	while(done < _samples)
	{
		const int n = static_cast<int>(std::min<size_t>(kMaxBlock, _samples - done));
		float* channels[] = { _outputs[0] + done, _outputs[1] + done };
		m_out.setDataToReferTo(channels, 2, n);
		m_synth->processBlock(m_out, m_midi);
		m_midi.clear();
		done += static_cast<size_t>(n);
	}
}

bool Device::sendMidi(const synthLib::SMidiEvent& _ev, std::vector<synthLib::SMidiEvent>&)
{
	if(!_ev.sysex.empty() || _ev.a >= 0xf0)
		return true;

	const uint8_t status = _ev.a & 0xf0;
	const auto offset = static_cast<int>(_ev.offset);

	switch(status)
	{
	case 0x80:
	case 0x90:
	case 0xe0:
		m_midi.addEvent(juce::MidiMessage(_ev.a, _ev.b, _ev.c), offset);
		return true;
	case 0xc0:
		m_pendingProgram.store(_ev.b);
		return true;
	case 0xb0:
		break;
	default:
		return true;
	}

	if(_ev.b == 1 || _ev.b == 120 || _ev.b == 123)
	{
		m_midi.addEvent(juce::MidiMessage(_ev.a, _ev.b, _ev.c), offset);
		return true;
	}

	for(const auto& d : kCcs)
	{
		if(d.cc != _ev.b)
			continue;
		if(auto* p = m_synth->apvts.getParameter(d.id))
			p->setValueNotifyingHost(ccToNormalised(*p, d, _ev.c));
		break;
	}
	return true;
}

std::vector<std::pair<uint8_t, uint8_t>> Device::getCcValues() const
{
	std::vector<std::pair<uint8_t, uint8_t>> out;
	for(const auto& d : kCcs)
		if(auto* p = m_synth->apvts.getParameter(d.id))
			out.emplace_back(d.cc, normalisedToCc(*p, d, p->getValue()));
	return out;
}

std::vector<std::pair<uint8_t, uint8_t>> Device::takePanelEdits()
{
	std::vector<std::pair<uint8_t, uint8_t>> out;
	const uint64_t mask = m_synth->takeEditedParams();
	if(mask == 0)
		return out;

	const auto& params = m_synth->getParameters();
	for(int i = 0; i < params.size() && i < 64; ++i)
	{
		if(!(mask & (uint64_t(1) << i)))
			continue;
		auto* p = dynamic_cast<juce::RangedAudioParameter*>(params[i]);
		if(!p)
			continue;
		if(const auto* d = ccDefFor(p->paramID))
			out.emplace_back(d->cc, normalisedToCc(*p, *d, p->getValue()));
	}
	return out;
}

bool Device::loadPreset(const std::string& _path)
{
	const juce::File file(juce::String::fromUTF8(_path.c_str()));
	juce::MemoryBlock data;
	if(!file.loadFileAsData(data))
		return false;

	juce::MemoryInputStream in(data, false);
	std::string name;
	juce::MemoryBlock chunk;
	if(!readFxp(in, name, chunk))
		return false;

	const auto values = presetValues(chunk.getData(), static_cast<int>(chunk.getSize()));
	if(values.empty())
		return false;

	m_synth->applyPreset(values);
	m_patchName = !name.empty() ? name : file.getFileNameWithoutExtension().toStdString();
	return true;
}

bool Device::savePreset(const std::string& _path, const std::string& _name) const
{
	const juce::File file(juce::String::fromUTF8(_path.c_str()));
	file.getParentDirectory().createDirectory();
	const auto data = fxpData(_name, presetChunk(m_synth->getPresetValues()));
	return file.replaceWithData(data.getData(), data.getSize());
}

std::vector<std::string> Device::extractBank(const std::string& _fxbPath, const std::string& _folder)
{
	std::vector<std::string> written;
	juce::MemoryBlock data;
	if(!juce::File(juce::String::fromUTF8(_fxbPath.c_str())).loadFileAsData(data))
		return written;

	const juce::File dir(juce::String::fromUTF8(_folder.c_str()));
	auto writeProgram = [&](const std::string& _name, const juce::MemoryBlock& _chunk)
	{
		const auto legal = juce::File::createLegalFileName(juce::String::fromUTF8(_name.c_str())).trim();
		auto file = dir.getChildFile((legal.isEmpty() ? juce::String("Preset") : legal) + ".fxp");
		if(file.existsAsFile())
			file = file.getNonexistentSibling(true);
		dir.createDirectory();
		const auto fxp = fxpData(_name, _chunk);
		if(file.replaceWithData(fxp.getData(), fxp.getSize()))
			written.push_back(file.getFullPathName().toStdString());
	};

	juce::MemoryInputStream in(data, false);
	if(in.readIntBigEndian() != 0x43636e4b)     // 'CcnK'
		return written;
	in.readIntBigEndian();
	const auto kind = in.readIntBigEndian();
	in.skipNextBytes(12);                        // version, fx id, fx version
	const int numPrograms = in.readIntBigEndian();
	in.skipNextBytes(128);                       // future

	if(kind == 0x46424368)                       // 'FBCh': one chunk holding every program
	{
		const auto chunkSize = static_cast<int>(in.readIntBigEndian());
		if(chunkSize <= 0 || in.getNumBytesRemaining() < chunkSize)
			return written;
		juce::MemoryBlock chunk(static_cast<size_t>(chunkSize));
		in.read(chunk.getData(), chunkSize);
		auto xml = juce::AudioProcessor::getXmlFromBinary(chunk.getData(), chunkSize);
		if(!xml)
			return written;
		int index = 1;
		for(auto* prg : xml->getChildWithTagNameIterator("PROGRAM"))
		{
			juce::XmlElement single("CA72");
			for(const auto* e : prg->getChildWithTagNameIterator("PARAM"))
				single.addChildElement(new juce::XmlElement(*e));
			juce::MemoryBlock one;
			juce::AudioProcessor::copyXmlToBinary(single, one);
			const auto name = prg->getStringAttribute("name", "Preset " + juce::String(index++));
			writeProgram(name.toStdString(), one);
		}
	}
	else if(kind == 0x4678426b)                  // 'FxBk': a list of programs
	{
		for(int i = 0; i < numPrograms; ++i)
		{
			std::string name;
			juce::MemoryBlock chunk;
			if(!readFxp(in, name, chunk))
				break;
			writeProgram(name.empty() ? "Preset " + std::to_string(i + 1) : name, chunk);
		}
	}
	return written;
}

bool Device::writeBank(const std::string& _fxbPath, const std::vector<std::string>& _fxpPaths)
{
	juce::XmlElement bank("CA72Bank");
	int count = 0;
	for(const auto& path : _fxpPaths)
	{
		juce::MemoryBlock data;
		const juce::File file(juce::String::fromUTF8(path.c_str()));
		if(!file.loadFileAsData(data))
			continue;
		juce::MemoryInputStream in(data, false);
		std::string name;
		juce::MemoryBlock chunk;
		if(!readFxp(in, name, chunk))
			continue;
		auto* prg = bank.createNewChildElement("PROGRAM");
		prg->setAttribute("name", name.empty() ? file.getFileNameWithoutExtension() : juce::String::fromUTF8(name.c_str()));
		for(const auto& [id, value] : presetValues(chunk.getData(), static_cast<int>(chunk.getSize())))
		{
			auto* e = prg->createNewChildElement("PARAM");
			e->setAttribute("id", id);
			e->setAttribute("value", static_cast<double>(value));
		}
		++count;
	}
	if(count == 0)
		return false;

	juce::MemoryBlock chunk;
	juce::AudioProcessor::copyXmlToBinary(bank, chunk);

	juce::MemoryOutputStream out;
	out.writeIntBigEndian(0x43636e4b);           // 'CcnK'
	out.writeIntBigEndian(static_cast<int>(152 + chunk.getSize()));
	out.writeIntBigEndian(0x46424368);           // 'FBCh'
	out.writeIntBigEndian(1);
	out.writeIntBigEndian(static_cast<int>(kFxpId));
	out.writeIntBigEndian(1);
	out.writeIntBigEndian(count);
	char future[128] = {};
	out.write(future, sizeof(future));
	out.writeIntBigEndian(static_cast<int>(chunk.getSize()));
	out.write(chunk.getData(), chunk.getSize());

	const juce::File file(juce::String::fromUTF8(_fxbPath.c_str()));
	return file.replaceWithData(out.getData(), out.getDataSize());
}

void Device::writeFactoryPresets(const std::string& _folder)
{
	const juce::File dir(juce::String::fromUTF8(_folder.c_str()));
	dir.createDirectory();

	for(const auto& p : ca72Presets)
	{
		const auto file = dir.getChildFile(juce::File::createLegalFileName(p.name) + ".fxp");
		if(file.existsAsFile())
			continue;
		std::map<juce::String, float> values;
		for(const auto& [id, value] : p.values)
			values[id] = value;
		const auto data = fxpData(p.name, presetChunk(values));
		file.replaceWithData(data.getData(), data.getSize());
	}
}

#if SYNTHLIB_DEMO_MODE == 0
bool Device::getState(std::vector<uint8_t>& _state, const synthLib::StateType _type)
{
	if(_type != synthLib::StateTypeGlobal)
		return false;
	juce::MemoryBlock block;
	m_synth->getStateInformation(block);
	const auto* d = static_cast<const uint8_t*>(block.getData());
	_state.assign(d, d + block.getSize());
	return true;
}

bool Device::setState(const std::vector<uint8_t>& _state, const synthLib::StateType _type)
{
	if(_type != synthLib::StateTypeGlobal || _state.empty())
		return false;
	m_synth->setStateInformation(_state.data(), static_cast<int>(_state.size()));
	return true;
}
#endif
} // namespace ca72Lib
