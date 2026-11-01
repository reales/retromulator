#pragma once
#include <array>
#include <cstdint>

#include "Patch.h"
#include "Regs.h"

// EG register mapping, fitted from measurements/eg (see fits/eg/FIT.md)
namespace dx7clean::eg
{
constexpr uint8_t rateReg(uint8_t rate)
{
	return uint8_t((41 * (rate > 99 ? 99 : rate)) >> 6);
}

constexpr std::array<uint8_t, 19> kLowLevel = {63, 61, 59, 57, 55, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 42, 41};

constexpr uint8_t levelReg(uint8_t level)
{
	if (level > 99)
		level = 99;
	return level < kLowLevel.size() ? kLowLevel[level] : uint8_t((99 - level) >> 1);
}

constexpr uint8_t sensReg(uint8_t rateScaling, uint8_t ams)
{
	return uint8_t((rateScaling & 7) | ((ams & 3) << 3));
}

// patch load only: note on/off never touch these registers, rate scaling per note runs in the EGS
template<typename Write> void writeRatesLevels(const Vced& v, Write&& write)
{
	for (int o = 0; o < regs::kOps; ++o)
	{
		const int b = regs::regOpToVcedBlock(o);
		for (int s = 0; s < 4; ++s)
			write(uint16_t(regs::kEgRate + 4 * o + s), rateReg(v[vced::op(b, vced::Op(vced::R1 + s))]));
	}
	for (int o = 0; o < regs::kOps; ++o)
	{
		const int b = regs::regOpToVcedBlock(o);
		for (int s = 0; s < 4; ++s)
			write(uint16_t(regs::kEgLevel + 4 * o + s), levelReg(v[vced::op(b, vced::Op(vced::L1 + s))]));
	}
}

template<typename Write> void writeSens(const Vced& v, Write&& write)
{
	for (int o = 0; o < regs::kOps; ++o)
	{
		const int b = regs::regOpToVcedBlock(o);
		write(uint16_t(regs::kOpSens + o), sensReg(v[vced::op(b, vced::RS)], v[vced::op(b, vced::AMS)]));
	}
}
}
