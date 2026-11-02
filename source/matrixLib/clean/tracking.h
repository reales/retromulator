#pragma once

#include <array>
#include <cstdint>

#include "types.h"

namespace matrixClean
{
	// INTERFACE.md 4.6. Unipolar inputs use s = Mod / 64 (0-255); bipolar inputs are offset to
	// s = level + 128 and the output is re-centred. Level = Mod / bipolarDiv: 128 for the LFOs and
	// vibrato (lfoCurve::toMod), 64 for lever 1 (matrix::bend); 0 = unipolar.
	int bipolarDiv(Source _s);
	Mod trackingGenerator(Mod _input, int _bipolarDiv, const std::array<uint8_t, 5>& _points);
}
