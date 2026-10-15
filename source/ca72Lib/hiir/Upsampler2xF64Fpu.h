/*****************************************************************************

        Upsampler2xF64Fpu.h
        Author: Laurent de Soras, 2020

Upsamples by a factor 2 the input signal.

Template parameters:
	- NC: number of coefficients, > 0

--- Legal stuff ---

This program is free software. It comes without any warranty, to
the extent permitted by applicable law. You can redistribute it
and/or modify it under the terms of the Do What The Fuck You Want
To Public License, Version 2, as published by Sam Hocevar. See
http://www.wtfpl.net/ for more details.

*Tab=3***********************************************************************/



#pragma once
#if ! defined (hiir_Upsampler2xF64Fpu_HEADER_INCLUDED)
#define hiir_Upsampler2xF64Fpu_HEADER_INCLUDED



/*\\\ INCLUDE FILES \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/

#include "Upsampler2xTpl.h"



namespace hiir
{



template <int NC>
using Upsampler2xF64Fpu = Upsampler2xTpl <NC, double, 1>;



}  // namespace hiir



//#include "Upsampler2xF64Fpu.hpp"



#endif   // hiir_Upsampler2xF64Fpu_HEADER_INCLUDED



/*\\\ EOF \\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\\*/
