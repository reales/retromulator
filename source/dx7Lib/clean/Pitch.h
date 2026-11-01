#pragma once
#include <array>
#include <cstdint>

#include "Patch.h"
#include "Regs.h"

// Key, operator frequency and detune registers, fitted from measurements/pitch (see fits/pitch/FIT.md)
namespace dx7clean::pitch
{
constexpr std::array<uint16_t, 32> kCoarse = {
	0, 4096, 8192, 10588, 12288, 13608, 14684, 15596, 16384, 17080, 17704, 18266, 18780, 19252, 19692, 20100,
	20480, 20840, 21176, 21496, 21800, 22088, 22362, 22624, 22876, 23116, 23348, 23572, 23788, 23994, 24196, 24388};
constexpr std::array<uint16_t, 4> kFixedBase = {5805, 19411, 33017, 46625};
constexpr uint16_t kRatioBase = 4908;
constexpr uint16_t kFixedFineStep = 136;
// 2 * floor(2048 * log2(1 + f / 100))
constexpr std::array<uint16_t, 100> kFine = {
	0, 58, 116, 174, 230, 288, 344, 398, 454, 508, 562, 616, 668, 722, 774, 824, 876, 926, 978, 1026,
	1076, 1126, 1174, 1222, 1270, 1318, 1364, 1412, 1458, 1504, 1550, 1594, 1640, 1684, 1728, 1772, 1816, 1860, 1902, 1944,
	1988, 2030, 2072, 2112, 2154, 2194, 2236, 2276, 2316, 2356, 2396, 2434, 2474, 2512, 2550, 2588, 2626, 2664, 2702, 2740,
	2776, 2814, 2850, 2886, 2922, 2958, 2994, 3030, 3064, 3100, 3134, 3170, 3204, 3238, 3272, 3306, 3340, 3374, 3406, 3440,
	3472, 3506, 3538, 3570, 3602, 3634, 3666, 3698, 3730, 3760, 3792, 3822, 3854, 3884, 3916, 3946, 3976, 4006, 4036, 4066};

constexpr uint16_t opWord(uint8_t mode, uint8_t coarse, uint8_t fine)
{
	coarse &= 31;
	if (fine > 99)
		fine = 99;
	if (mode & 1)
		return uint16_t(kFixedBase[coarse & 3] + kFixedFineStep * fine);
	return uint16_t(kRatioBase + kCoarse[coarse] + kFine[fine]);
}

// 16-bit voice pitch word for a played note: key = note + transpose - 24, 3 semitones = 1024
constexpr uint16_t keyWord(int key)
{
	if (key < 0 || key > 127)
		key = 127;
	if (key < 1)
		key = 1;
	const int g = key / 3, r = key % 3;
	constexpr int lo[3] = {0, 88, 172};
	return uint16_t(((4 * g + 36 + r) << 8) + lo[r]);
}

constexpr int transposedKey(uint8_t note, uint8_t transpose)
{
	return int(note) + int(transpose > 48 ? 48 : transpose) - 24;
}

// master tune -256..255 adds straight to the word
constexpr uint16_t voiceWord(uint8_t note, uint8_t transpose, int masterTune)
{
	const int w = int(keyWord(transposedKey(note, transpose))) + masterTune;
	return uint16_t(w < 0 ? 0 : w > 0xffff ? 0xffff : w);
}

// key word every voice holds at power-on (idle voices write 5120 = this with the pitch EG at 0x4000)
constexpr uint16_t kPowerOnKeyWord = 21504;

constexpr uint8_t detuneReg(uint8_t dt)
{
	if (dt > 14)
		dt = 14;
	return dt < 7 ? uint8_t(8 | (7 - dt)) : uint8_t(dt - 7);
}

inline uint16_t opWord(const Vced& v, int regOp)
{
	const int b = regs::regOpToVcedBlock(regOp);
	return opWord(v[vced::op(b, vced::MODE)], v[vced::op(b, vced::FC)], v[vced::op(b, vced::FF)]);
}

template<typename Write> void writeOpPitch(const Vced& v, Write&& write)
{
	for (int o = 0; o < regs::kOps; ++o)
	{
		const uint16_t w = opWord(v, o);
		write(uint16_t(regs::kOpPitch + 2 * o), uint8_t(w >> 8));
		write(uint16_t(regs::kOpPitch + 2 * o + 1), uint8_t(w & 0xff));
	}
}

template<typename Write> void writeDetune(const Vced& v, Write&& write)
{
	for (int o = 0; o < regs::kOps; ++o)
		write(uint16_t(regs::kOpDetune + o), detuneReg(v[vced::op(regs::regOpToVcedBlock(o), vced::DT)]));
}

template<typename Write> void writeVoiceWord(int voice, uint16_t w, Write&& write)
{
	write(uint16_t(regs::kVoicePitch + 2 * voice), uint8_t(w >> 8));
	write(uint16_t(regs::kVoicePitch + 2 * voice + 1), uint8_t(w & 0xff));
}
}
