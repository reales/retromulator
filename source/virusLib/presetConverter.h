#pragma once

#include "microcontrollerTypes.h"
#include "romfile.h"

#include "synthLib/midiTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace virusLib
{

// Converts Virus single patches between A/B/C versions (downgrade path).
// The main use case is C→B and C→A, remapping parameters that don't exist
// on the older model to the closest safe equivalent.
//
// Patch layout (256 bytes, two pages):
//   Page A = bytes 0–127   (CC-accessible parameters)
//   Page B = bytes 128–255
//
// Byte 0 = PresetVersion tag:  A=0x00, B=0x06, C=0x07
//
// Key parameters affected by downgrade:
//
//   Byte 51 (0x33)  Filter1 Mode
//     A/B: 0=LP  1=HP  2=BP  3=BS
//     C:   4=Analog1P  5=Analog2P  6=Analog3P  7=Analog4P
//
//   Byte 52 (0x34)  Filter2 Mode
//     Same values; C addendum says "Filter 2 always remains the original
//     2-pole Virus filter", but the parameter byte can still hold 4–7.
//
//   Byte 49 (0x31)  Saturation Curve
//     A/B: 0=Off 1=Light 2=Soft 3=Middle 4=Hard 5=Digital 6=Shaper
//     When the Analog filter is active on C, saturation is implicit in the
//     filter algorithm and the regular curve is ignored.  On downgrade we
//     restore a sensible default if the patch relied on analog saturation.
//
//   Byte 53 (0x35)  Filter Routing
//     0=Ser4  1=Ser6  2=Par4  3=Split
//     "While the analogue filter is active, there is no difference between
//      Serial 6 and Serial 4 modes." – OS 6.5 addendum.
//     No value remapping needed; both modes exist on all models.

class PresetConverter
{
public:
	// Byte offsets within the 256-byte single-patch data
	static constexpr uint8_t kVersionByte      = 0;
	static constexpr uint8_t kSaturationCurve  = 49;  // 0x31
	static constexpr uint8_t kFilter1Mode      = 51;  // 0x33
	static constexpr uint8_t kFilter2Mode      = 52;  // 0x34
	static constexpr uint8_t kFilterRouting    = 53;  // 0x35

	// Filter mode values
	static constexpr uint8_t kFilterLP   = 0;
	static constexpr uint8_t kFilterHP   = 1;
	static constexpr uint8_t kFilterBP   = 2;
	static constexpr uint8_t kFilterBS   = 3;
	static constexpr uint8_t kAnalog1P   = 4;  // C only
	static constexpr uint8_t kAnalog2P   = 5;  // C only
	static constexpr uint8_t kAnalog3P   = 6;  // C only
	static constexpr uint8_t kAnalog4P   = 7;  // C only

	// Saturation curve values
	static constexpr uint8_t kSatOff     = 0;
	static constexpr uint8_t kSatLight   = 1;
	static constexpr uint8_t kSatSoft    = 2;
	static constexpr uint8_t kSatMiddle  = 3;
	static constexpr uint8_t kSatHard    = 4;
	static constexpr uint8_t kSatDigital = 5;
	static constexpr uint8_t kSatShaper  = 6;

	// Convert a single preset (256-byte array) to the target version.
	// Returns true if any modification was made.
	static bool convertPreset(ROMFile::TPreset& _preset, PresetVersion _targetVersion);

	// Convert a raw sysex DUMP_SINGLE message in-place.
	// Patch data starts at byte offset 9 in the sysex message.
	// Returns true if conversion was performed.
	static bool convertSysexMessage(synthLib::SysexBuffer& _sysex, PresetVersion _targetVersion);

	// Convert an entire bank of sysex messages in-place.
	// Returns the number of patches that were modified.
	static int convertSysexBank(synthLib::SysexBufferList& _messages, PresetVersion _targetVersion);

	// Convert a bank and write the result to a .syx file.
	// Returns true on success.
	static bool convertBankToFile(const synthLib::SysexBufferList& _messages,
	                              PresetVersion _targetVersion,
	                              const std::string& _destPath);

	// Returns a human-readable summary of what was changed.
	static std::string describeConversion(PresetVersion _from, PresetVersion _to);

private:
	// Remap Filter1/Filter2 mode values from C→B or C→A.
	// Analog filter types (4–7) are mapped to LP (0), the closest general-
	// purpose lowpass behaviour.
	static uint8_t remapFilterMode(uint8_t _value, PresetVersion _target);

	// If a C patch used the Analog filter (which has implicit saturation),
	// and the saturation byte is Off, set it to a reasonable default so
	// the B/A version doesn't sound completely dry.
	static uint8_t fixSaturationForAnalogDowngrade(uint8_t _currentSat, bool _hadAnalogFilter);

	// Recalculate the sysex checksum for Access Virus messages.
	static uint8_t calcChecksum(const synthLib::SysexBuffer& _data, size_t _start, size_t _end);
};

} // namespace virusLib
