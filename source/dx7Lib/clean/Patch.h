#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace dx7clean
{
constexpr size_t kVcedSize = 155;
constexpr size_t kVmemVoiceSize = 128;
constexpr size_t kVoicesPerBank = 32;
constexpr size_t kVmemSize = kVmemVoiceSize * kVoicesPerBank;
constexpr size_t kOpBlockSize = 21;

using Vced = std::array<uint8_t, kVcedSize>;
using VmemVoice = std::array<uint8_t, kVmemVoiceSize>;

namespace vced
{
enum Op : uint8_t
{
	R1 = 0, R2, R3, R4, L1, L2, L3, L4,
	BP = 8, LD, RD, LC, RC, RS, AMS, KVS, OL, MODE, FC, FF, DT
};
enum Voice : uint8_t
{
	PR1 = 126, PR2, PR3, PR4, PL1, PL2, PL3, PL4,
	ALG = 134, FB, OKS, LFS, LFD, LPMD, LAMD, LFKS, LFW, PMS, TRNP, NAME = 145
};
constexpr size_t kOpMaskIndex = 155;
inline size_t op(int vcedBlock, Op p) { return size_t(vcedBlock) * kOpBlockSize + p; }
}

Vced initVoice();
void clamp(Vced& v);
std::string voiceName(const Vced& v);

VmemVoice pack(const Vced& v);
Vced unpack(const uint8_t* packed);

uint8_t checksum(const uint8_t* data, size_t len);

std::vector<uint8_t> makeSingleSysex(const Vced& v, uint8_t channel);
std::vector<uint8_t> makeBankSysex(const std::array<VmemVoice, kVoicesPerBank>& bank, uint8_t channel);
}
