#include "modulators.h"

#include <cmath>

namespace matrixClean
{
	namespace curves
	{
		double envStageUs(const int _value) { return _value <= 0 ? 0.0 : 500.0 * std::pow(2.0, _value / 5.0); }
		double rampUs(const int _rate) { return envStageUs(_rate); }
	}
}
