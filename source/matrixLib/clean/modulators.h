#pragma once

#include <array>
#include <cstdint>

#include "env.h"
#include "lfo.h"
#include "types.h"

namespace matrixClean
{
	// Placeholder curves. Every function here is a stub to be replaced from measurements.
	namespace curves
	{
		double envStageUs(int _value);          // [R-ENV] 0-63 (+ matrix offset) to stage time
		double rampUs(int _rate);               // [R-RAMP]
	}

}
