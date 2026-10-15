/*
OpenB3: an open source sound synthesis engine and JUCE application/plugin that simulates
the magnificent sound of the Hammond B3 organ and Leslie rotating speaker
Copyright (C) 2021-2022 Michele Perrone
Github: https://github.com/michele-perrone/OpenPiano
Author e-mail: perrone(dot)michele(at)outlook(dot)com
This program is free software: you can redistribute it and/or modify
it under the terms of the GNU Affero General Public License as published
by the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU Affero General Public License for more details.

You should have received a copy of the GNU Affero General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef GLOBAL_DEFINITIONS_H
#define GLOBAL_DEFINITIONS_H

#ifdef __cplusplus
extern "C" {
#endif

#define MIN(A, B) (((A) < (B)) ? (A) : (B))

#define UPPER_MANUAL 0
#define LOWER_MANUAL 1
#define PEDAL_BOARD  2

extern double SampleRateD;

extern unsigned int defaultPresetUpperManual[9];
extern unsigned int defaultPresetLowerManual[9];
extern unsigned int defaultPresetPedalBoard[9];

#ifdef __cplusplus
}
#endif

#endif
