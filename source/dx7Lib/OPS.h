/**
 *  VDX7 - Virtual DX7 synthesizer emulation
 *  Copyright (C) 2023  chiaccona@gmail.com
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
**/

#pragma once
#include <cstdint>
#include <cmath>
#include <cstring>

#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
#include <arm_neon.h>
#define DX7_NEON 1
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define DX7_SSE2 1
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifdef INLINE_EGS
#define DX7_INLINE __attribute__ ((always_inline))
#else
#define DX7_INLINE
#endif

namespace dx7Emu {

struct logsin_t  {
	logsin_t() = default;
	logsin_t(uint16_t v) { val = v; }
	operator uint16_t&() { return val; }
	uint16_t val=0;
	bool sign=false;
};

struct SinTab {
	static const int LG_N = 10;
	static const int N = 1<<LG_N;
	SinTab() {
		for(int i=0; i<N; i++) {
			table[i] = int(.5002-N*std::log(std::sin((i+.5)/N*M_PI/2))/std::log(2.0));
		}
	}
	logsin_t operator()(uint32_t v) const {
		logsin_t s = table[ ((v&(1<<LG_N)) ? ~v : v) & (N-1) ];
		s.sign = v&(1<<(LG_N+1));
		return s;
	}
	uint16_t table[N];
};

struct ExpTab {
	ExpTab() {
		for(int i=0; i<16*N; i++) {
			table[i] = (uint32_t(.5+2*N*std::pow(2.0, double(i&(N-1))/N)) << ((i>>LG_N)&0xF)) >> 5;
		}
	}
	uint32_t get22(uint32_t v) const { return table[v]; }
	uint32_t get14(uint32_t v) const { return get22(v)>>8; }

	int32_t invertLogSinClean(logsin_t ls) const { return ls.sign ? -static_cast<int32_t>(get14(ls)) : static_cast<int32_t>(get14(ls)); }

	int32_t invertLogSin(logsin_t ls) const { return ls.sign ? -static_cast<int32_t>(shift(get14(ls))) : static_cast<int32_t>(shift(get14(ls))); }
	uint32_t shift(uint32_t v) const {
		if(v&0xFFFFF000) v &= ~uint32_t(0b111);
		else if(v&0xFFFFF800) v &= ~uint32_t(0b11);
		else if(v&0xFFFFFC00) v &= ~uint32_t(0b1);
		return v;
	}

	static const int LG_N = 10;
	static const int N = 1<<LG_N;
	uint32_t table[16*N];
};

class OPS {
public:
	OPS(uint16_t (*f)[16], uint16_t (*e)[16])
		: frequency(f), envelope(e)
		{ }

	alignas(16) int32_t  out[16] = {0};

private:
	uint32_t phase[6][16] = {0};
	alignas(16) int32_t  fren1[16] = {0};
	alignas(16) int32_t  fren2[16] = {0};
	alignas(16) int32_t  mren[16] = {0};

	int order[16] = { 0,  8, 4, 12, 2, 10, 6, 14, 1,  9, 5, 13, 3, 11, 7, 15 };

	alignas(16) int32_t  modout[16] = {0};
	alignas(16) int32_t  signal[16] = {0};
	uint8_t  com[16] = {0};

	uint16_t comtab[6] = {
		0b00000<<7, 0b01000<<7, 0b01101<<7,
		0b10000<<7, 0b10011<<7, 0b10101<<7
	};

	static const SinTab sintab;
	static const ExpTab exptab;

	enum SEL { SEL0, SEL1, SEL2, SEL3, SEL4, SEL5 };
	struct algoROM_t { SEL sel; bool A, C, D; uint8_t COM; };
	static const algoROM_t algoROM[32][6];

public:
	void setAlgorithm(uint8_t byte1, uint8_t byte2) {
		if(!(byte1&(1<<7)) && !(byte1&(1<<2))) {
			if(byte1&(1<<6)) keySync = false;
				else if(byte1&(1<<5)) keySync = true;
			if(byte1&(1<<4)) {
				for(int i=0; i<16; i++) {
					algorithm[i] = byte2>>3;
					feedback[i] = byte2&0x7;
					updateLanes(i);
				}
			} else {
				algorithm[byte1&0xF] = byte2>>3;
				feedback[byte1&0xF] = byte2&0x7;
				updateLanes(byte1&0xF);
			}
		}
	}
private:
	uint8_t  algorithm[16] = {0};
	uint8_t  feedback[16] = {0};

	// algoROM and feedback per voice as lane masks for clockOp
	struct lanes_t {
		alignas(16) int32_t A[16], C[16], D[16], sel1[16], sel2[16], sel3[16], sel4[16], sel5[16], fbShift[16];
		uint8_t COM[16];
	};
	lanes_t lanes[6];
	bool lanesInit = initLanes();

	bool initLanes() { for(int v=0; v<16; v++) updateLanes(v); return true; }
	void updateLanes(int v) {
		for(int op=0; op<6; op++) {
			const algoROM_t& a = algoROM[algorithm[v]][op];
			lanes_t& l = lanes[op];
			l.A[v] = a.A ? -1 : 0;
			l.C[v] = a.C ? -1 : 0;
			l.D[v] = a.D ? -1 : 0;
			l.sel1[v] = a.sel == SEL1 ? -1 : 0;
			l.sel2[v] = a.sel == SEL2 ? -1 : 0;
			l.sel3[v] = a.sel == SEL3 ? -1 : 0;
			l.sel4[v] = a.sel == SEL4 ? -1 : 0;
			l.sel5[v] = a.sel == SEL5 ? -1 : 0;
			l.fbShift[v] = 1 + (7 - feedback[v]);
			l.COM[v] = a.COM;
		}
	}
	bool     keySync = false;

	uint16_t (*frequency)[16];
	uint16_t (*envelope)[16];

	bool clean_ = false;

public:
	void clean(bool v) { clean_ = v; }

	void keyOn(int n) {
		if(keySync) for(int i=0; i<6; i++) phase[i][n] = 0;
	}

	static uint32_t phaseIncrement(uint16_t f) { return exptab.get22(f); }

	// Same result as clock(op, v) for v = 0..15 in order. Table lookups stay scalar, the rest runs 4 voices per vector
	void DX7_INLINE clockOp(int op, const uint32_t* inc) {
		alignas(16) int32_t ls[16];
		alignas(16) int32_t env[16];
		uint32_t* ph = phase[op];

		for(int v=0; v<16; v+=4) {
#if defined(DX7_NEON)
			const uint32x4_t p = vld1q_u32(ph + v);
			vst1q_s32(ls + v, vreinterpretq_s32_u32(vaddq_u32(vshrq_n_u32(p, 11), vreinterpretq_u32_s32(vld1q_s32(modout + v)))));
			vst1q_u32(ph + v, vandq_u32(vaddq_u32(p, vld1q_u32(inc + v)), vdupq_n_u32((1<<23)-1)));
#elif defined(DX7_SSE2)
			const __m128i p = _mm_loadu_si128(reinterpret_cast<const __m128i*>(ph + v));
			_mm_store_si128(reinterpret_cast<__m128i*>(ls + v), _mm_add_epi32(_mm_srli_epi32(p, 11), _mm_load_si128(reinterpret_cast<const __m128i*>(modout + v))));
			_mm_storeu_si128(reinterpret_cast<__m128i*>(ph + v), _mm_and_si128(_mm_add_epi32(p, _mm_loadu_si128(reinterpret_cast<const __m128i*>(inc + v))), _mm_set1_epi32((1<<23)-1)));
#else
			for(int i=v; i<v+4; i++) {
				ls[i] = static_cast<int32_t>((ph[i] >> 11) + static_cast<uint32_t>(modout[i]));
				ph[i] = (ph[i] + inc[i]) & ((1<<23)-1);
			}
#endif
		}

		int32_t neg[16];
		for(int v=0; v<16; v++) {
			const uint32_t phi = static_cast<uint32_t>(ls[v]);
			neg[v] = -static_cast<int32_t>((phi >> (SinTab::LG_N+1)) & 1);
			ls[v] = sintab.table[((phi & (1<<SinTab::LG_N)) ? ~phi : phi) & (SinTab::N-1)] + comtab[com[v]];
			env[v] = envelope[op][v];
		}

		for(int v=0; v<16; v+=4) {
#if defined(DX7_NEON)
			int32x4_t x = vandq_s32(vaddq_s32(vld1q_s32(ls + v), vshlq_n_s32(vld1q_s32(env + v), 2)), vdupq_n_s32(0xFFFF));
			const uint32x4_t over = vtstq_s32(x, vdupq_n_s32(0x4000));
			x = vbslq_s32(over, vdupq_n_s32(0x3FFF), x);
			vst1q_s32(ls + v, veorq_s32(x, vdupq_n_s32(0x3FFF)));
#elif defined(DX7_SSE2)
			__m128i x = _mm_and_si128(_mm_add_epi32(_mm_load_si128(reinterpret_cast<const __m128i*>(ls + v)), _mm_slli_epi32(_mm_load_si128(reinterpret_cast<const __m128i*>(env + v)), 2)), _mm_set1_epi32(0xFFFF));
			const __m128i over = _mm_cmpeq_epi32(_mm_and_si128(x, _mm_set1_epi32(0x4000)), _mm_set1_epi32(0x4000));
			x = _mm_or_si128(_mm_and_si128(over, _mm_set1_epi32(0x3FFF)), _mm_andnot_si128(over, x));
			_mm_store_si128(reinterpret_cast<__m128i*>(ls + v), _mm_xor_si128(x, _mm_set1_epi32(0x3FFF)));
#else
			for(int i=v; i<v+4; i++) {
				int32_t x = (ls[i] + (env[i] << 2)) & 0xFFFF;
				x = (x & 0x4000) ? 0x3FFF : x;
				ls[i] = x ^ 0x3FFF;
			}
#endif
		}

		for(int v=0; v<16; v++) {
			uint32_t m = exptab.get14(static_cast<uint32_t>(ls[v]));
			if(!clean_) m = exptab.shift(m);
			signal[v] = (static_cast<int32_t>(m) ^ neg[v]) - neg[v];
		}

		const lanes_t& l = lanes[op];
#if !defined(DX7_NEON)
		alignas(16) int32_t fb[16];
		for(int v=0; v<16; v++) fb[v] = (fren1[v] + fren2[v]) >> l.fbShift[v];
#endif
		for(int v=0; v<16; v+=4) {
#if defined(DX7_NEON)
			const int32x4_t sig = vld1q_s32(signal + v), mr = vld1q_s32(mren + v), f1 = vld1q_s32(fren1 + v), f2 = vld1q_s32(fren2 + v);
			const uint32x4_t A = vreinterpretq_u32_s32(vld1q_s32(l.A + v));
			const int32x4_t msum = vaddq_s32(vandq_s32(mr, vld1q_s32(l.C + v)), vandq_s32(sig, vld1q_s32(l.D + v)));
			const int32x4_t fbv = vshlq_s32(vaddq_s32(f1, f2), vnegq_s32(vld1q_s32(l.fbShift + v)));
			int32x4_t mo = vandq_s32(sig, vld1q_s32(l.sel1 + v));
			mo = vorrq_s32(mo, vandq_s32(msum, vld1q_s32(l.sel2 + v)));
			mo = vorrq_s32(mo, vandq_s32(mr, vld1q_s32(l.sel3 + v)));
			mo = vorrq_s32(mo, vandq_s32(f1, vld1q_s32(l.sel4 + v)));
			mo = vorrq_s32(mo, vandq_s32(fbv, vld1q_s32(l.sel5 + v)));
			vst1q_s32(modout + v, mo);
			vst1q_s32(mren + v, msum);
			vst1q_s32(fren2 + v, vbslq_s32(A, f1, f2));
			vst1q_s32(fren1 + v, vbslq_s32(A, sig, f1));
#elif defined(DX7_SSE2)
			auto ld = [](const int32_t* p) { return _mm_load_si128(reinterpret_cast<const __m128i*>(p)); };
			auto st = [](int32_t* p, __m128i x) { _mm_store_si128(reinterpret_cast<__m128i*>(p), x); };
			const __m128i sig = ld(signal + v), mr = ld(mren + v), f1 = ld(fren1 + v), f2 = ld(fren2 + v), A = ld(l.A + v);
			const __m128i msum = _mm_add_epi32(_mm_and_si128(mr, ld(l.C + v)), _mm_and_si128(sig, ld(l.D + v)));
			__m128i mo = _mm_and_si128(sig, ld(l.sel1 + v));
			mo = _mm_or_si128(mo, _mm_and_si128(msum, ld(l.sel2 + v)));
			mo = _mm_or_si128(mo, _mm_and_si128(mr, ld(l.sel3 + v)));
			mo = _mm_or_si128(mo, _mm_and_si128(f1, ld(l.sel4 + v)));
			mo = _mm_or_si128(mo, _mm_and_si128(ld(fb + v), ld(l.sel5 + v)));
			st(modout + v, mo);
			st(mren + v, msum);
			st(fren2 + v, _mm_or_si128(_mm_and_si128(A, f1), _mm_andnot_si128(A, f2)));
			st(fren1 + v, _mm_or_si128(_mm_and_si128(A, sig), _mm_andnot_si128(A, f1)));
#else
			for(int i=v; i<v+4; i++) {
				const int32_t sig = signal[i], mr = mren[i], f1 = fren1[i], f2 = fren2[i];
				const int32_t msum = (mr & l.C[i]) + (sig & l.D[i]);
				modout[i] = (sig & l.sel1[i]) | (msum & l.sel2[i]) | (mr & l.sel3[i]) | (f1 & l.sel4[i]) | (fb[i] & l.sel5[i]);
				mren[i] = msum;
				fren2[i] = (f1 & l.A[i]) | (f2 & ~l.A[i]);
				fren1[i] = (sig & l.A[i]) | (f1 & ~l.A[i]);
			}
#endif
		}
		std::memcpy(com, l.COM, 16);
		if(op==5) for(int v=0; v<16; v++) out[order[v]] = mren[v];
	}

	void DX7_INLINE clock(int op, int voice) {
		uint32_t phi = phase[op][voice];
		phase[op][voice] += exptab.get22(frequency[op][voice]);
		phase[op][voice] &= (1<<23)-1;
		phi >>= 11;
		phi += modout[voice];

		logsin_t logsin = sintab(phi);
		logsin += envelope[op][voice]<<2;
		logsin += comtab[com[voice]];
		if(logsin&0x4000) logsin = 0x3FFF;
		logsin ^= 0x3FFF;

		if(clean_) signal[voice] = exptab.invertLogSinClean(logsin);
			else signal[voice] = exptab.invertLogSin(logsin);

		const algoROM_t& algo = algoROM[ algorithm[voice] ][op];

		int32_t msum = 0;
		if(algo.C) msum += mren[voice];
		if(algo.D) msum += signal[voice];

		switch(algo.sel) {
			case SEL0: modout[voice] = 0; break;
			case SEL1: modout[voice] = signal[voice]; break;
			case SEL2: modout[voice] = msum; break;
			case SEL3: modout[voice] = mren[voice]; break;
			case SEL4: modout[voice] = fren1[voice]; break;
			case SEL5: modout[voice] = (fren1[voice]+fren2[voice])>>(1+(7-feedback[voice])); break;
		}
		mren[voice] = msum;
		if(algo.A) {
			fren2[voice] = fren1[voice];
			fren1[voice] = signal[voice];
		}
		com[voice] = algo.COM;

		if(op==5) out[order[voice]] = mren[voice];
	}
};

inline const SinTab OPS::sintab;
inline const ExpTab OPS::exptab;

inline const OPS::algoROM_t OPS::algoROM[32][6] = {
	{{SEL1,1,0,0,0}, {SEL1,0,0,0,0}, {SEL1,0,0,0,1}, {SEL0,0,0,1,0}, {SEL1,0,1,0,1}, {SEL5,0,1,1,0}},
	{{SEL1,0,0,0,0}, {SEL1,0,0,0,0}, {SEL1,0,0,0,1}, {SEL5,0,0,1,0}, {SEL1,1,1,0,1}, {SEL0,0,1,1,0}},
	{{SEL1,1,0,0,0}, {SEL1,0,0,0,1}, {SEL0,0,0,1,0}, {SEL1,0,1,0,0}, {SEL1,0,1,0,1}, {SEL5,0,1,1,0}},
	{{SEL1,0,0,0,0}, {SEL1,0,0,0,1}, {SEL0,1,0,1,0}, {SEL1,0,1,0,0}, {SEL1,0,1,0,1}, {SEL5,0,1,1,0}},
	{{SEL1,1,0,0,2}, {SEL0,0,0,1,0}, {SEL1,0,1,0,2}, {SEL0,0,1,1,0}, {SEL1,0,1,0,2}, {SEL5,0,1,1,0}},
	{{SEL1,0,0,0,2}, {SEL0,1,0,1,0}, {SEL1,0,1,0,2}, {SEL0,0,1,1,0}, {SEL1,0,1,0,2}, {SEL5,0,1,1,0}},
	{{SEL1,1,0,0,0}, {SEL0,0,0,1,0}, {SEL2,0,1,1,1}, {SEL0,0,0,1,0}, {SEL1,0,1,0,1}, {SEL5,0,1,1,0}},
	{{SEL1,0,0,0,0}, {SEL5,0,0,1,0}, {SEL2,1,1,1,1}, {SEL0,0,0,1,0}, {SEL1,0,1,0,1}, {SEL0,0,1,1,0}},
	{{SEL1,0,0,0,0}, {SEL0,0,0,1,0}, {SEL2,0,1,1,1}, {SEL5,0,0,1,0}, {SEL1,1,1,0,1}, {SEL0,0,1,1,0}},
	{{SEL0,0,0,1,0}, {SEL2,0,1,1,1}, {SEL5,0,0,1,0}, {SEL1,1,1,0,0}, {SEL1,0,1,0,1}, {SEL0,0,1,1,0}},
	{{SEL0,1,0,1,0}, {SEL2,0,1,1,1}, {SEL0,0,0,1,0}, {SEL1,0,1,0,0}, {SEL1,0,1,0,1}, {SEL5,0,1,1,0}},
	{{SEL0,0,0,1,0}, {SEL0,0,1,1,0}, {SEL2,0,1,1,1}, {SEL5,0,0,1,0}, {SEL1,1,1,0,1}, {SEL0,0,1,1,0}},
	{{SEL0,1,0,1,0}, {SEL0,0,1,1,0}, {SEL2,0,1,1,1}, {SEL0,0,0,1,0}, {SEL1,0,1,0,1}, {SEL5,0,1,1,0}},
	{{SEL0,1,0,1,0}, {SEL2,0,1,1,0}, {SEL1,0,0,0,1}, {SEL0,0,0,1,0}, {SEL1,0,1,0,1}, {SEL5,0,1,1,0}},
	{{SEL0,0,0,1,0}, {SEL2,0,1,1,0}, {SEL1,0,0,0,1}, {SEL5,0,0,1,0}, {SEL1,1,1,0,1}, {SEL0,0,1,1,0}},
	{{SEL1,1,0,0,0}, {SEL0,0,0,1,0}, {SEL1,0,1,0,0}, {SEL0,0,1,1,0}, {SEL2,0,1,1,0}, {SEL5,0,0,1,0}},
	{{SEL1,0,0,0,0}, {SEL0,0,0,1,0}, {SEL1,0,1,0,0}, {SEL5,0,1,1,0}, {SEL2,1,1,1,0}, {SEL0,0,0,1,0}},
	{{SEL1,0,0,0,0}, {SEL1,0,0,0,0}, {SEL5,0,0,1,0}, {SEL0,1,1,1,0}, {SEL2,0,1,1,0}, {SEL0,0,0,1,0}},
	{{SEL1,1,0,0,2}, {SEL4,0,0,1,2}, {SEL0,0,1,1,0}, {SEL1,0,1,0,0}, {SEL1,0,1,0,2}, {SEL5,0,1,1,0}},
	{{SEL0,0,0,1,0}, {SEL2,0,1,1,2}, {SEL5,0,0,1,0}, {SEL1,1,1,0,2}, {SEL4,0,1,1,2}, {SEL0,0,1,1,0}},
	{{SEL1,0,0,1,3}, {SEL3,0,0,1,3}, {SEL5,0,1,1,0}, {SEL1,1,1,0,3}, {SEL4,0,1,1,3}, {SEL0,0,1,1,0}},
	{{SEL1,1,0,0,3}, {SEL4,0,0,1,3}, {SEL4,0,1,1,3}, {SEL0,0,1,1,0}, {SEL1,0,1,0,3}, {SEL5,0,1,1,0}},
	{{SEL1,1,0,0,3}, {SEL4,0,0,1,3}, {SEL0,0,1,1,0}, {SEL1,0,1,0,3}, {SEL0,0,1,1,3}, {SEL5,0,1,1,0}},
	{{SEL1,1,0,0,4}, {SEL4,0,0,1,4}, {SEL4,0,1,1,4}, {SEL0,0,1,1,4}, {SEL0,0,1,1,4}, {SEL5,0,1,1,0}},
	{{SEL1,1,0,0,4}, {SEL4,0,0,1,4}, {SEL0,0,1,1,4}, {SEL0,0,1,1,4}, {SEL0,0,1,1,4}, {SEL5,0,1,1,0}},
	{{SEL0,1,0,1,0}, {SEL2,0,1,1,2}, {SEL0,0,0,1,0}, {SEL1,0,1,0,2}, {SEL0,0,1,1,2}, {SEL5,0,1,1,0}},
	{{SEL0,0,0,1,0}, {SEL2,0,1,1,2}, {SEL5,0,0,1,0}, {SEL1,1,1,0,2}, {SEL0,0,1,1,2}, {SEL0,0,1,1,0}},
	{{SEL5,0,0,1,0}, {SEL1,1,1,0,0}, {SEL1,0,1,0,2}, {SEL0,0,1,1,0}, {SEL1,0,1,0,2}, {SEL0,0,1,1,2}},
	{{SEL1,1,0,0,3}, {SEL0,0,0,1,0}, {SEL1,0,1,0,3}, {SEL0,0,1,1,3}, {SEL0,0,1,1,3}, {SEL5,0,1,1,0}},
	{{SEL5,0,0,1,0}, {SEL1,1,1,0,0}, {SEL1,0,1,0,3}, {SEL0,0,1,1,3}, {SEL0,0,1,1,3}, {SEL0,0,1,1,3}},
	{{SEL1,1,0,0,4}, {SEL0,0,0,1,4}, {SEL0,0,1,1,4}, {SEL0,0,1,1,4}, {SEL0,0,1,1,4}, {SEL5,0,1,1,0}},
	{{SEL0,1,0,1,5}, {SEL0,0,1,1,5}, {SEL0,0,1,1,5}, {SEL0,0,1,1,5}, {SEL0,0,1,1,5}, {SEL5,0,1,1,5}},
};

} // namespace dx7Emu
