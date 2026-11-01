#pragma once
#include <cstdint>

namespace dx7clean::regs
{
constexpr uint16_t kEgs = 0x3000;
constexpr uint16_t kVoicePitch = kEgs + 0x00;
constexpr uint16_t kOpPitch = kEgs + 0x20;
constexpr uint16_t kOpDetune = kEgs + 0x30;
constexpr uint16_t kEgRate = kEgs + 0x40;
constexpr uint16_t kEgLevel = kEgs + 0x60;
constexpr uint16_t kOpLevel = kEgs + 0x80;
constexpr uint16_t kOpSens = kEgs + 0xE0;
constexpr uint16_t kAmpMod = kEgs + 0xF0;
constexpr uint16_t kVoiceEvent = kEgs + 0xF1;
constexpr uint16_t kPitchMod = kEgs + 0xF2;

constexpr uint16_t kOpsMode = 0x2804;
constexpr uint16_t kOpsAlg = 0x2805;
constexpr uint16_t kVolumeDac = 0x280A;

constexpr uint8_t kModeAllVoices = 0x10;
constexpr uint8_t kModeKeySyncOn = 0x20;
constexpr uint8_t kModeKeySyncOff = 0x40;

constexpr int kVoices = 16;
constexpr int kOps = 6;

constexpr uint32_t kCyclesPerSecond = 1178304;
constexpr uint32_t kCyclesPerSample = 24;
constexpr double kSampleRate = 49096.0;
constexpr int kLogPerOctave = 1024;

// register op 0 = patch OP6 = VCED operator block 0
inline int regOpToVcedBlock(int regOp) { return regOp; }
}
