/*************************************************************************************
 * Original code copyright (C) 2012 Steve Folta
 * Converted to Juce module (C) 2016 Leo Olivers
 * Forked from https://github.com/stevefolta/SFZero
 * For license info please see the LICENSE file distributed with this source code
 *************************************************************************************/
#ifndef SF2WINTYPES_H_INCLUDED
#define SF2WINTYPES_H_INCLUDED

#include "SFZCommon.h"

#define FourCCEquals(value1, value2)                                                                                             \
  (value1[0] == value2[0] && value1[1] == value2[1] && value1[2] == value2[2] && value1[3] == value2[3])
// Useful in printfs:
#define FourCCArgs(value) (value)[0], (value)[1], (value)[2], (value)[3]

namespace sfzero
{
typedef char fourcc[4];
typedef unsigned char byte;
typedef unsigned long dword;
typedef unsigned short word;

// Special types for SF2 fields.
typedef char char20[20];
}

#endif // SF2WINTYPES_H_INCLUDED
