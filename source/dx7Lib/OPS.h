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

	int32_t  out[16] = {0};

private:
	uint32_t phase[6][16] = {0};
	int32_t  fren1[16] = {0};
	int32_t  fren2[16] = {0};
	int32_t  mren[16] = {0};

	int order[16] = { 0,  8, 4, 12, 2, 10, 6, 14, 1,  9, 5, 13, 3, 11, 7, 15 };

	int32_t  modout[16] = {0};
	int32_t  signal[16] = {0};
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
				}
			} else {
				algorithm[byte1&0xF] = byte2>>3;
				feedback[byte1&0xF] = byte2&0x7;
			}
		}
	}
private:
	uint8_t  algorithm[16] = {0};
	uint8_t  feedback[16] = {0};
	bool     keySync = false;

	uint16_t (*frequency)[16];
	uint16_t (*envelope)[16];

	bool clean_ = false;

public:
	void clean(bool v) { clean_ = v; }

	void keyOn(int n) {
		if(keySync) for(int i=0; i<6; i++) phase[i][n] = 0;
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
