#include "Patch.h"

namespace dx7clean
{
namespace
{
constexpr uint8_t kOpMax[kOpBlockSize] = {99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 99, 3, 3, 7, 3, 7, 99, 1, 31, 99, 14};
constexpr uint8_t kVoiceMax[19] = {99, 99, 99, 99, 99, 99, 99, 99, 31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48};
}

Vced initVoice()
{
	Vced v{};
	for (int b = 0; b < 6; ++b)
	{
		for (int s = 0; s < 4; ++s)
			v[vced::op(b, vced::Op(vced::R1 + s))] = 99;
		v[vced::op(b, vced::L1)] = 99;
		v[vced::op(b, vced::L2)] = 99;
		v[vced::op(b, vced::L3)] = 99;
		v[vced::op(b, vced::L4)] = 0;
		v[vced::op(b, vced::BP)] = 39;
		v[vced::op(b, vced::OL)] = b == 5 ? 99 : 0;
		v[vced::op(b, vced::FC)] = 1;
		v[vced::op(b, vced::DT)] = 7;
	}
	for (int i = 0; i < 4; ++i)
	{
		v[vced::PR1 + i] = 99;
		v[vced::PL1 + i] = 50;
	}
	v[vced::OKS] = 1;
	v[vced::LFS] = 35;
	v[vced::LFKS] = 1;
	v[vced::PMS] = 3;
	v[vced::TRNP] = 24;
	const char* name = "INIT VOICE";
	for (int i = 0; i < 10; ++i)
		v[vced::NAME + i] = uint8_t(name[i]);
	return v;
}

void clamp(Vced& v)
{
	for (size_t i = 0; i < 126; ++i)
		if (v[i] > kOpMax[i % kOpBlockSize])
			v[i] = kOpMax[i % kOpBlockSize];
	for (size_t i = 0; i < 19; ++i)
		if (v[126 + i] > kVoiceMax[i])
			v[126 + i] = kVoiceMax[i];
	for (size_t i = vced::NAME; i < kVcedSize; ++i)
		v[i] &= 0x7f;
}

std::string voiceName(const Vced& v)
{
	return std::string(reinterpret_cast<const char*>(&v[vced::NAME]), 10);
}

VmemVoice pack(const Vced& v)
{
	VmemVoice p{};
	for (int b = 0; b < 6; ++b)
	{
		const uint8_t* o = &v[size_t(b) * kOpBlockSize];
		uint8_t* d = &p[size_t(b) * 17];
		for (int i = 0; i < 11; ++i)
			d[i] = o[i];
		d[11] = uint8_t((o[vced::RC] & 3) << 2 | (o[vced::LC] & 3));
		d[12] = uint8_t((o[vced::DT] & 15) << 3 | (o[vced::RS] & 7));
		d[13] = uint8_t((o[vced::KVS] & 7) << 2 | (o[vced::AMS] & 3));
		d[14] = o[vced::OL];
		d[15] = uint8_t((o[vced::FC] & 31) << 1 | (o[vced::MODE] & 1));
		d[16] = o[vced::FF];
	}
	for (int i = 0; i < 8; ++i)
		p[102 + i] = v[vced::PR1 + i];
	p[110] = v[vced::ALG] & 31;
	p[111] = uint8_t((v[vced::OKS] & 1) << 3 | (v[vced::FB] & 7));
	p[112] = v[vced::LFS];
	p[113] = v[vced::LFD];
	p[114] = v[vced::LPMD];
	p[115] = v[vced::LAMD];
	p[116] = uint8_t((v[vced::PMS] & 7) << 4 | (v[vced::LFW] & 7) << 1 | (v[vced::LFKS] & 1));
	p[117] = v[vced::TRNP];
	for (int i = 0; i < 10; ++i)
		p[118 + i] = v[vced::NAME + i] & 0x7f;
	return p;
}

Vced unpack(const uint8_t* p)
{
	Vced v{};
	for (int b = 0; b < 6; ++b)
	{
		const uint8_t* s = p + b * 17;
		uint8_t* o = &v[size_t(b) * kOpBlockSize];
		for (int i = 0; i < 11; ++i)
			o[i] = s[i] & 0x7f;
		o[vced::LC] = s[11] & 3;
		o[vced::RC] = (s[11] >> 2) & 3;
		o[vced::RS] = s[12] & 7;
		o[vced::DT] = (s[12] >> 3) & 15;
		o[vced::AMS] = s[13] & 3;
		o[vced::KVS] = (s[13] >> 2) & 7;
		o[vced::OL] = s[14] & 0x7f;
		o[vced::MODE] = s[15] & 1;
		o[vced::FC] = (s[15] >> 1) & 31;
		o[vced::FF] = s[16] & 0x7f;
	}
	for (int i = 0; i < 8; ++i)
		v[vced::PR1 + i] = p[102 + i] & 0x7f;
	v[vced::ALG] = p[110] & 31;
	v[vced::FB] = p[111] & 7;
	v[vced::OKS] = (p[111] >> 3) & 1;
	v[vced::LFS] = p[112] & 0x7f;
	v[vced::LFD] = p[113] & 0x7f;
	v[vced::LPMD] = p[114] & 0x7f;
	v[vced::LAMD] = p[115] & 0x7f;
	v[vced::LFKS] = p[116] & 1;
	v[vced::LFW] = (p[116] >> 1) & 7;
	v[vced::PMS] = (p[116] >> 4) & 7;
	v[vced::TRNP] = p[117] & 0x7f;
	for (int i = 0; i < 10; ++i)
		v[vced::NAME + i] = p[118 + i] & 0x7f;
	clamp(v);
	return v;
}

uint8_t checksum(const uint8_t* data, size_t len)
{
	unsigned sum = 0;
	for (size_t i = 0; i < len; ++i)
		sum += data[i];
	return uint8_t((0u - sum) & 0x7f);
}

std::vector<uint8_t> makeSingleSysex(const Vced& v, uint8_t channel)
{
	std::vector<uint8_t> s = {0xf0, 0x43, uint8_t(channel & 15), 0x00, 0x01, 0x1b};
	s.insert(s.end(), v.begin(), v.end());
	s.push_back(checksum(v.data(), v.size()));
	s.push_back(0xf7);
	return s;
}

std::vector<uint8_t> makeBankSysex(const std::array<VmemVoice, kVoicesPerBank>& bank, uint8_t channel)
{
	std::vector<uint8_t> s = {0xf0, 0x43, uint8_t(channel & 15), 0x09, 0x20, 0x00};
	for (const auto& p : bank)
		s.insert(s.end(), p.begin(), p.end());
	s.push_back(checksum(s.data() + 6, kVmemSize));
	s.push_back(0xf7);
	return s;
}
}
