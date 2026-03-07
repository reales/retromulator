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

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <cmath>

namespace dx7Emu {

struct LP1 {
	LP1(double d = 1.) { set (d); y1 = 0.; }
	void reset() { y1 = 0.; }
	void set_f(float fc) { set (1.0f - std::exp(-2.0f * static_cast<float>(M_PI) * fc)); }
	void set(float d) { a0 = d; b1 = 1.0f - d; }
	float operate(float x) { return y1 = a0*x + b1*y1; }
	float a0, b1, y1;
};

struct LP {
	LP() { }
	void reset() { x1 = y1 = 0.; }
	float operate(float s) {
		y1 = s + b1*x1 - a0*y1;
		x1 = s;
		return y1;
	}
	float a0=0.0f, b1=0.0f, y1=0.0f, x1=0.0f;
};

struct SOS_Coeff { float b1, b2, a1, a2; };
struct SOS {
	SOS_Coeff coeff;
	int h=0;
	float x[2]={0}, y[2]={0};

	SOS() = default;
	SOS(const SOS_Coeff& x) : coeff(x) {}

	float operate(float s) {
		float r = s;
		r += coeff.b1 * x[h]; r -= coeff.a1 * y[h];
		h ^= 1;
		r += coeff.b2 * x[h]; r -= coeff.a2 * y[h];
		y[h] = r; x[h] = s;
		return r;
	}
};

struct Filter {
	LP lp;
	SOS sos1;
	SOS sos2;
	float gain;

	Filter() {
		lp.b1 = 1.0000065695182569f;
		lp.a0 = -0.9471494282369527f;
		sos1.coeff.b1 = 1.9999934304817428f;
		sos1.coeff.b2 = 0.9999934305249014f;
		sos1.coeff.a1 = -1.9047157177069487f;
		sos1.coeff.a2 = 0.9129212928486624f;
		sos2.coeff.b1 = 2.0000000000000000f;
		sos2.coeff.b2 = 1.0000000000000000f;
		sos2.coeff.a1 = -1.9531729648773684f;
		sos2.coeff.a2 = 0.9694025617460298f;
		gain = 2.1994620400553497e-07f;
	}

	float operate(float s) {
		return gain * sos2.operate( sos1.operate( lp.operate(s) ) );
	}
};

} // namespace dx7Emu
