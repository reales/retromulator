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

#include "global_definitions.h"

double SampleRateD = 0; // Initialized to some value for Apple CLang compatibility.
                        // Actual sample rate assigned by BeatrixCPP's constructor

unsigned int defaultPresetUpperManual[9] = { 8, 8, 6, 0, 0, 0, 0, 0, 0 };
unsigned int defaultPresetLowerManual[9] = { 8, 8, 8, 8, 0, 0, 0, 0, 8 };
unsigned int defaultPresetPedalBoard[9] =  { 8, 0, 0, 0, 0, 0, 0, 0, 0 };
