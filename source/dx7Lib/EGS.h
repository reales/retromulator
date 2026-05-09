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
#include <cstdio>

#ifdef INLINE_EGS
#define DX7_INLINE __attribute__ ((always_inline))
#else
#define DX7_INLINE
#endif

#include "OPS.h"
#include "filter.h"

namespace dx7Emu {

struct Envelope {
	Envelope() { }

	enum Stage { S0, S1, S2, S3 };

	uint8_t *rates=0, *levels=0;
    uint8_t *outparam=0;
	bool keyoff = false;
	uint8_t ratescaling = 0;

	int16_t level = 0xFF0;
	int16_t target = 0xFF0;

	bool rising = false;
	Stage stage = S3;
	bool compress = true;

	uint16_t *clock = 0;
	uint8_t nshift = 0, pshift = 0;
	uint16_t small = 0;
	uint8_t mask = 0;

	void key_on() { keyoff = false; advance(); }
	void key_off() { keyoff = true; advance(); }

	void init(uint8_t *r, uint8_t *l, uint8_t *op, uint16_t *clk) {
    	rates = r; levels = l; outparam = op; clock = clk;
		key_off();
	}

	uint16_t getsample() {
		if (stage>1 && (level == target)) return level;
		if ((!(*clock & small))
			&& (mask & (1<<((*clock>>nshift)&7)))
			) {
			if (rising) {
				if (level>0x94C) level = 0x94C;
				int slope = (level>>8) + 2;
				level -= slope<<pshift;
				if (level <= target) {
					level = target;
					advance();
				}
			} else {
				level += 1<<pshift;
				if (level >= target) {
					level = target;
					advance();
				}
			}
		}
		return level;
	}

	void advance() {
		switch(stage) {
			case S0:
				if(keyoff) { stage = S3; compress = true; }
				else { stage = S1; compress = false; }
				break;
			case S1: if(keyoff) stage = S3; else stage = S2; break;
			case S2: if(keyoff) stage = S3; else return; break;
			case S3: if(!keyoff) stage = S0; else return; break;
		}
		updateLevel();
		updateRate();
	}
	void updateLevel() {
		target = (levels[stage]<<6) + ((*outparam)<<4);
		if(target>0xFF0) target = 0xFF0;

		int prev = (stage+3)&3;
		if(stage<2 && levels[stage] >= 40 && levels[prev] >= 40) {
			int delay = (stage==0 && compress) ? 15 : 479;
			target = level + delay;
			if(target>0xFF0) {
				level = 0xFF0-delay;
				target = 0xFF0;
			}
		}
		rising = (target < level);
	}

	void updateRate() {
		uint8_t qrate = rates[stage]+ratescaling;
		if(qrate>63) qrate = 63;

		pshift = nshift = small = 0;
		if(qrate<44) {
			nshift = 11 - (qrate>>2);
			small = (1<<nshift) - 1;
		} else pshift = (qrate>>2) - 11;

		mask = outmask[qrate&3];
	}

	static constexpr const uint8_t outmask[4] = { 0xAA, 0xEA, 0xEE, 0xFE };
};

class EGS {
public:
	EGS(uint8_t *m) :
		mem(m),
		opDetune(m+0x30),
		opEGrates((uint8_t(*)[4])(m+0x40)),
		opEGlevels((uint8_t(*)[4])(m+0x60)),
		opLevels((uint8_t(*)[16])(m+0x80)),
		opSensScale(m+0xE0),
		ampMod(*(m+0xF0)),
		voiceEvents(*(m+0xF1)),
		ops(frequency, envelope)
	{
		for(int i=0; i<256; i++) mem[i] = 0xFF;
		for(int op=0; op<6; op++) {
			for(int voice=0; voice<16; voice++) {
				env[op][voice].init(opEGrates[op], opEGlevels[op], &(opLevels[op][voice]), &env_clock);
			}
		}
	}

private:
	uint8_t *mem;

	uint16_t voicePitch[16]={0};
	uint16_t opPitch[6]={0};
	uint8_t *opDetune;
	uint8_t (*opEGrates)[4];
	uint8_t (*opEGlevels)[4];
	uint8_t (*opLevels)[16];
	uint8_t *opSensScale;
	uint8_t &ampMod;
	uint8_t &voiceEvents;
	int16_t pitchMod=0;

	Envelope env[6][16];
	int currOp=0, currVoice=0;
	uint16_t env_clock = 0;

	uint16_t frequency[6][16] = {0};
	uint16_t envelope[6][16] = {0};

	OPS ops;

	Filter skFilter;
	float filter(int32_t *out) {
		float ret = 0;
		constexpr const float cgain = 1.0f/float(1<<15);
		constexpr const float gain = 16.0f/float(1<<15);
		if(clean_) for(int v=0; v<16; v++) ret += cgain * out[v];
		else for(int v=0; v<16; v++) ret = skFilter.operate(gain * out[v]);
		return ret;
	}

	bool clean_ = false;

public:
	void clean(bool v) { clean_ = v; ops.clean(v); }

	void setAlgorithm(uint8_t mode, uint8_t algo) { ops.setAlgorithm(mode, algo); }

	void DX7_INLINE clock(float* outbuf, int &count, int cycles) {
		for(int i=0; i<cycles; i++) {

			uint16_t e = env[currOp][currVoice].getsample();

			int ampModSens = (opSensScale[currOp]>>3);
			if(ampModSens) e += ampMod<<ampModSens;

			if(e>0xFFF) e = 0xFFF;
			envelope[currOp][currVoice] = e;

			ops.clock(currOp, currVoice);

			if(++currVoice == 16) {
				currVoice = 0;
				if(++currOp == 6) {
					outbuf[count++] = filter(ops.out);
					currOp = 0;
					env_clock++;
				}
			}
		}
	}

	void update(uint8_t ADDR) {
		switch(ADDR>>5) {
			case 0:
				if(ADDR&0x01) { updateVoicePitch(ADDR>>1); return; }
			case 1:
				if(ADDR&0x01) { updateOpPitch((ADDR&0x0F)>>1); return; }
				return;
			case 2:
				if((ADDR>=0x40) && (ADDR<0x57)) {
					if((ADDR&0x03)!=3) return;
					uint8_t op = (ADDR - 0x40)>>2;
					for(int voice=0; voice<16; voice++) env[op][voice].updateRate();
				}
				return;
			case 3:
				if((ADDR>=0x60) && (ADDR<0x77)) {
					if((ADDR&0x03)!=3) return;
					uint8_t op = (ADDR - 0x60)>>2;
					for(int voice=0; voice<16; voice++) env[op][voice].updateLevel();
				}
				return;
			case 4:
			case 5:
			case 6:
				return;
			case 7:
				if(ADDR>=0xE0 && ADDR <=0xE5) {
					uint8_t op = ADDR-0xE0;
					for(int voice=0; voice<16; voice++) updateRateScaling(voice, op);
				}
				else if(ADDR==0xF3) { updatePitchMod(); return; }
				else if(ADDR==0xF1) { updateVoiceEvents(); return; }
				return;
		}
	}

private:
	void updateVoiceEvents() {
		uint8_t voice = voiceEvents>>2;
		bool keyon = voiceEvents&1;
		for(int op=0; op<6; op++) {
			if(keyon) env[op][voice].key_on();
			else env[op][voice].key_off();
		}
		if(keyon) ops.keyOn(voice);
	}

	void updateRateScaling(uint8_t voice, uint8_t op) {
		int rateScaling = opSensScale[op]&0x7;
		uint16_t rs = static_cast<uint16_t>(min(27,max(0,(voicePitch[voice]>>8)-16))*rateScaling/7.0 + 0.5);
		env[op][voice].ratescaling = static_cast<uint8_t>(rs);
	}

	void updateVoicePitch(uint8_t voice) {
		voicePitch[voice] = (mem[2*voice]<<8 | mem[2*voice+1])>>2;
		updateFrequency(voice);
		for(int op=0; op<6; op++) updateRateScaling(voice, op);
	}

	void updateOpPitch(uint8_t op) { opPitch[op] = mem[0x20+2*op]<<8 | mem[0x20+2*op+1]; }

	void updatePitchMod() {
		pitchMod = static_cast<int16_t>(mem[0xF2]<<8 | mem[0xF3])/16;
		for(int voice=0; voice<16; voice++) updateFrequency(voice);
	}

	void updateFrequency(uint8_t voice) {
		for(int op=0; op<6; op++) {
			int32_t f = opPitch[op]>>2;
			f += opDetune[op]&0x8 ? -static_cast<int16_t>(opDetune[op]&0x7) : opDetune[op];
			if(!(opPitch[op]&1)) {
				f += voicePitch[voice] - 171;
				f += pitchMod;
			}
			if(f>0x3FFF) f=0x3FFF; else if(f<0) f=0;
			frequency[op][voice] = static_cast<uint16_t>(f);
		}
	}

	int max(int x, int y) { return x>y ? x : y; }
	int min(int x, int y) { return x<y ? x : y; }
};

} // namespace dx7Emu
