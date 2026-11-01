#pragma once
#include <array>
#include <cstdint>

#include "Patch.h"
#include "Regs.h"

// Operator output level register, fitted from measurements/level (see fits/level/FIT.md)
namespace dx7clean::level
{
constexpr int outputScaled(uint8_t ol)
{
	if (ol > 99)
		ol = 99;
	if (ol == 0)
		return 0;
	if (ol < 5)
		return 4 * ol + 1;
	if (ol < 7)
		return 3 * ol + 5;
	if (ol < 16)
		return 2 * ol + 11;
	if (ol < 20)
		return ((3 * ol) >> 1) + 18;
	return ol + 28;
}

// velocity >> 2 = 0-7, no compact form
constexpr std::array<int8_t, 8> kVelocityLow = {112, 108, 102, 100, 98, 93, 90, 87};

// velocity offset per kvs step, in 1/8 register units
constexpr int velocityCurve(uint8_t velocity)
{
	velocity &= 0x7f;
	if (velocity < 32)
		return kVelocityLow[velocity >> 2];
	if (velocity < 64)
		return 126 - 6 * (velocity >> 2);
	if (velocity < 84)
		return 36 - 6 * ((velocity - 60) >> 3);
	if (velocity >= 108 && velocity < 116)
		return -12;
	const int x = 14 - 9 * ((velocity - 84) >> 3);
	return x < -16 ? -16 : x;
}

// key groups 36-38 (break point 0-2 right, 96-99 left) don't follow either curve
constexpr std::array<uint8_t, 3> kCurveLinFar = {127, 122, 118};
constexpr std::array<uint8_t, 3> kCurveExpFar = {0, 8, 16};

constexpr int curveLin(int g)
{
	if (g >= 36)
		return kCurveLinFar[g - 36];
	if (g == 22)
		return 178;
	return g < 32 ? 8 * g : 255;
}

constexpr double sqrtc(double x)
{
	double r = x;
	for (int i = 0; i < 64; ++i)
		r = 0.5 * (r + x / r);
	return r;
}

// floor(2.105 * 2^(g/4) - 0.1), groups 19 and 27 excepted
constexpr int curveExp(int g)
{
	if (g >= 36)
		return kCurveExpFar[g - 36];
	if (g < 8)
		return g;
	if (g == 19)
		return 57;
	if (g == 27)
		return 224;
	const double q = sqrtc(sqrtc(2.0));
	double x = 2.105 * double(1 << (g >> 2));
	for (int i = 0; i < (g & 3); ++i)
		x *= q;
	const int p = int(x - 0.1);
	return p > 255 ? 255 : p;
}

constexpr int depthScaled(uint8_t depth)
{
	return ((depth > 99 ? 99 : depth) * 165) >> 6;
}

// played note + transpose (24 = none); out of range wraps as a byte and clamps to 127
constexpr uint8_t keyNote(uint8_t note, uint8_t transpose)
{
	const int n = uint8_t(note + (transpose > 48 ? 48 : transpose) - 24);
	return uint8_t(n > 127 ? 127 : n);
}

constexpr int keyGroup(uint8_t note)
{
	return (note > 0 ? note - 1 : 0) / 3;
}

// note is the transposed key (keyNote); curve 0 -LIN, 1 -EXP, 2 +EXP, 3 +LIN; result in scaled output level units
constexpr int scaling(uint8_t note, uint8_t bp, uint8_t leftDepth, uint8_t rightDepth, uint8_t leftCurve, uint8_t rightCurve)
{
	const int k = keyGroup(note & 0x7f) - ((bp > 99 ? 99 : bp) + 16) / 3;
	if (k == 0)
		return 0;
	const int g = k > 0 ? k : -k;
	const uint8_t depth = k > 0 ? rightDepth : leftDepth;
	const uint8_t curve = (k > 0 ? rightCurve : leftCurve) & 3;
	const int p = (curve == 0 || curve == 3) ? curveLin(g) : curveExp(g);
	const int v = (p * depthScaled(depth)) >> 8;
	return curve < 2 ? -v : v;
}

// register units; arithmetic shift rounds toward -inf
constexpr int velocityOffset(uint8_t kvs, uint8_t velocity)
{
	return ((kvs & 7) * velocityCurve(velocity)) >> 3;
}

constexpr uint8_t outputReg(uint8_t ol, uint8_t kvs, uint8_t velocity, uint8_t note, uint8_t bp,
	uint8_t leftDepth, uint8_t rightDepth, uint8_t leftCurve, uint8_t rightCurve)
{
	int l = outputScaled(ol) + scaling(note, bp, leftDepth, rightDepth, leftCurve, rightCurve);
	if (l > 127)
		l = 127;
	int a = 2 * (127 - l) + 15 + velocityOffset(kvs, velocity);
	return uint8_t(a < 4 ? 4 : a > 255 ? 255 : a);
}

constexpr uint8_t kOff = 255;

// opMask bit n = register op n (bit 0 = OP6, bit 5 = OP1), as VCED byte 155
constexpr uint8_t outputReg(const Vced& v, int regOp, uint8_t note, uint8_t velocity, uint8_t opMask = 0x3f)
{
	if (!(opMask & (1 << regOp)))
		return kOff;
	note = keyNote(note, v[vced::TRNP]);
	const int b = regs::regOpToVcedBlock(regOp);
	auto p = [&](vced::Op f) { return v[vced::op(b, f)]; };
	return outputReg(p(vced::OL), p(vced::KVS), velocity, note, p(vced::BP), p(vced::LD), p(vced::RD), p(vced::LC), p(vced::RC));
}

// note on, played note: the EGS picks the value up at the key on event that follows
template<typename Write> void writeOutputLevels(const Vced& v, uint8_t opMask, int voice, uint8_t note, uint8_t velocity, Write&& write)
{
	for (int o = 0; o < regs::kOps; ++o)
		write(uint16_t(regs::kOpLevel + 16 * o + voice), outputReg(v, o, note, velocity, opMask));
}
}
