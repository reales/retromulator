#pragma once

#include "Musashi/m68kcpu.h"

namespace mc68k
{
	class Mc68k;

	struct CpuState : m68ki_cpu_core
	{
		mc68k::Mc68k* instance = nullptr;
	};
}
