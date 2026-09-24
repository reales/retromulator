#include "presetConverter.h"
#include "microcontroller.h"

#include <algorithm>
#include <fstream>

namespace virusLib
{

// ── Filter mode remapping ──────────────────────────────────────────────────
//
// Virus C added Analog (Moog-style) filter modes as values 4–7 on CC 51/52.
// These don't exist on B or A — the DSP interprets them as garbage, producing
// distorted noise.
//
// Remapping strategy:
//   Analog 1P (4) → LP (0)   – single-pole analog is closest to a gentle LP
//   Analog 2P (5) → LP (0)   – 2-pole analog ≈ standard 2-pole LP
//   Analog 3P (6) → LP (0)   – 3-pole → LP (no 3-pole on A/B, LP is safest)
//   Analog 4P (7) → LP (0)   – 4-pole Moog ≈ classic 4-pole LP (24 dB/oct)
//
// All analog modes are lowpass by nature (the Moog ladder is inherently LP),
// so LP is the correct fallback.  The sound won't be identical, but it will
// be in the right ballpark rather than producing noise.

uint8_t PresetConverter::remapFilterMode(const uint8_t _value, const PresetVersion _target)
{
	if(_target >= C)
		return _value;  // C and above support all modes

	if(_value >= kAnalog1P)
		return kFilterLP;  // Analog modes → LP on B and A

	return _value;  // LP/HP/BP/BS are universal
}

// ── Saturation fix for analog filter downgrade ─────────────────────────────
//
// The Virus C analog filter has implicit saturation built into the algorithm.
// When active, the regular Saturation Curve parameter (CC 49) is ignored by
// the C firmware.  Sound designers often leave it at 0 (Off) since the analog
// filter provides its own warmth.
//
// When we downgrade to B/A, the analog filter is gone (remapped to LP) and
// the patch now runs through the standard filter with Saturation = Off.
// This makes the sound noticeably thinner/cleaner than intended.
//
// Fix: if the patch had an analog filter and saturation was Off, set it to
// "Soft" (2) as a reasonable stand-in for the analog filter's character.
// If saturation was already set to something, leave it alone — the designer
// intentionally chose a curve that will now take effect.

uint8_t PresetConverter::fixSaturationForAnalogDowngrade(const uint8_t _currentSat, const bool _hadAnalogFilter)
{
	if(!_hadAnalogFilter)
		return _currentSat;

	if(_currentSat == kSatOff)
		return kSatSoft;  // Analog warmth → Soft saturation is the gentlest approximation

	return _currentSat;
}

// ── Sysex checksum ─────────────────────────────────────────────────────────

uint8_t PresetConverter::calcChecksum(const synthLib::SysexBuffer& _data, const size_t _start, const size_t _end)
{
	uint8_t cs = 0;
	for(size_t i = _start; i < _end && i < _data.size(); ++i)
		cs += _data[i];
	return cs & 0x7f;
}

// ── Preset conversion (raw 256-byte patch data) ───────────────────────────

bool PresetConverter::convertPreset(ROMFile::TPreset& _preset, const PresetVersion _targetVersion)
{
	const auto sourceVersion = Microcontroller::getPresetVersion(_preset);

	// Nothing to do if already at or below target version
	if(sourceVersion <= _targetVersion)
		return false;

	// We only handle ABC family conversions (C→B, C→A, B→A)
	if(sourceVersion > C || _targetVersion > C)
		return false;

	bool modified = false;

	// Track whether the original patch used an analog filter (C-only feature)
	const bool hadAnalogFilter1 = _preset[kFilter1Mode] >= kAnalog1P;
	const bool hadAnalogFilter2 = _preset[kFilter2Mode] >= kAnalog1P;
	const bool hadAnalogFilter  = hadAnalogFilter1 || hadAnalogFilter2;

	// ── Filter1 Mode ───────────────────────────────────────────────────
	{
		const uint8_t oldVal = _preset[kFilter1Mode];
		const uint8_t newVal = remapFilterMode(oldVal, _targetVersion);
		if(newVal != oldVal)
		{
			_preset[kFilter1Mode] = newVal;
			modified = true;
		}
	}

	// ── Filter2 Mode ───────────────────────────────────────────────────
	// The OS 6.5 addendum says "Filter 2 always remains the original
	// 2-pole Virus filter", but patches can still have values 4–7 stored.
	{
		const uint8_t oldVal = _preset[kFilter2Mode];
		const uint8_t newVal = remapFilterMode(oldVal, _targetVersion);
		if(newVal != oldVal)
		{
			_preset[kFilter2Mode] = newVal;
			modified = true;
		}
	}

	// ── Saturation Curve ───────────────────────────────────────────────
	{
		const uint8_t oldSat = _preset[kSaturationCurve];
		const uint8_t newSat = fixSaturationForAnalogDowngrade(oldSat, hadAnalogFilter);
		if(newSat != oldSat)
		{
			_preset[kSaturationCurve] = newSat;
			modified = true;
		}
	}

	// ── Version byte ───────────────────────────────────────────────────
	// Always stamp the target version so the receiving firmware recognises
	// the patch as its own format.
	{
		const uint8_t targetByte = static_cast<uint8_t>(_targetVersion);
		if(_preset[kVersionByte] != targetByte)
		{
			_preset[kVersionByte] = targetByte;
			modified = true;
		}
	}

	return modified;
}

// ── Sysex message conversion ───────────────────────────────────────────────
//
// Access Virus single dump sysex format:
//   F0 00 20 33 01 <devID> 10 <bank> <program> <256 bytes> <checksum> F7
//   ^0  ^1 ^2 ^3 ^4  ^5   ^6   ^7      ^8       ^9...264     ^265   ^266
//
// Patch data starts at offset 9.  Checksum covers bytes 5 through 264 (inclusive).

bool PresetConverter::convertSysexMessage(synthLib::SysexBuffer& _sysex, const PresetVersion _targetVersion)
{
	// Minimum size: header(9) + 256 data + checksum(1) + F7(1) = 267
	if(_sysex.size() < 267)
		return false;

	// Verify Access Music manufacturer ID and DUMP_SINGLE command
	if(_sysex[0] != 0xF0 || _sysex[1] != 0x00 || _sysex[2] != 0x20 || _sysex[3] != 0x33)
		return false;
	if(_sysex[6] != 0x10)  // DUMP_SINGLE
		return false;

	constexpr size_t kDataOffset = 9;
	constexpr size_t kPatchSize  = 256;

	// Build a TPreset from the sysex data
	ROMFile::TPreset preset{};
	for(size_t i = 0; i < kPatchSize && (kDataOffset + i) < _sysex.size(); ++i)
		preset[i] = _sysex[kDataOffset + i];

	if(!convertPreset(preset, _targetVersion))
		return false;

	// Write converted data back into the sysex buffer
	for(size_t i = 0; i < kPatchSize; ++i)
		_sysex[kDataOffset + i] = preset[i];

	// Recalculate checksum (covers bytes 5 through data end)
	const size_t csPos = kDataOffset + kPatchSize;
	if(csPos < _sysex.size())
		_sysex[csPos] = calcChecksum(_sysex, 5, csPos);

	return true;
}

// ── Bank conversion ────────────────────────────────────────────────────────

int PresetConverter::convertSysexBank(synthLib::SysexBufferList& _messages, const PresetVersion _targetVersion)
{
	int count = 0;
	for(auto& msg : _messages)
	{
		if(convertSysexMessage(msg, _targetVersion))
			++count;
	}
	return count;
}

bool PresetConverter::convertBankToFile(const synthLib::SysexBufferList& _messages,
                                        const PresetVersion _targetVersion,
                                        const std::string& _destPath)
{
	// Make a mutable copy for conversion
	auto messages = _messages;
	convertSysexBank(messages, _targetVersion);

	// Concatenate all sysex messages into a single byte stream
	std::vector<uint8_t> output;
	for(const auto& msg : messages)
		output.insert(output.end(), msg.begin(), msg.end());

	if(output.empty())
		return false;

	std::ofstream file(_destPath, std::ios::binary | std::ios::trunc);
	if(!file.is_open())
		return false;

	file.write(reinterpret_cast<const char*>(output.data()), static_cast<std::streamsize>(output.size()));
	return file.good();
}

// ── Description ────────────────────────────────────────────────────────────

std::string PresetConverter::describeConversion(const PresetVersion _from, const PresetVersion _to)
{
	auto versionName = [](PresetVersion v) -> const char*
	{
		switch(v)
		{
		case A:  return "A";
		case B:  return "B";
		case C:  return "C";
		default: return "?";
		}
	};

	std::string result = "Virus ";
	result += versionName(_from);
	result += " -> ";
	result += versionName(_to);
	result += ": ";

	if(_from <= _to)
	{
		result += "no conversion needed";
		return result;
	}

	if(_from == C && _to <= B)
		result += "Analog filter modes remapped to LP; saturation adjusted for analog filter loss";
	else if(_from == B && _to == A)
		result += "Version byte updated (B and A share the same parameter set)";

	return result;
}

} // namespace virusLib
