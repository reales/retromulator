#pragma once

#include "cpuState.h"
#include "mc68k.h"
#include "memoryOps.h"

inline mc68k::Mc68k* mc68k_get_instance(m68ki_cpu_core* _core)
{
	return static_cast<mc68k::Mc68k*>(static_cast<mc68k::CpuState*>(_core)->instance);
}

// On GCC/Clang, weak+noinline allows multiple TUs to define these callbacks;
// the linker picks one. On MSVC, define them in exactly one TU by setting
// MUSASHI_ENTRY_IMPL before including this header (done in xtUc.cpp).
#if defined(_MSC_VER) && !defined(MUSASHI_ENTRY_IMPL)

extern "C"
{
	unsigned int m68k_read_immediate_16(m68ki_cpu_core* core, unsigned int address);
	unsigned int m68k_read_immediate_32(m68ki_cpu_core* core, unsigned int address);
	unsigned int m68k_read_pcrelative_8(m68ki_cpu_core* core, unsigned int address);
	unsigned int m68k_read_pcrelative_16(m68ki_cpu_core* core, unsigned int address);
	unsigned int m68k_read_pcrelative_32(m68ki_cpu_core* core, unsigned int address);
	unsigned int m68k_read_memory_8(m68ki_cpu_core* core, unsigned int address);
	unsigned int m68k_read_memory_16(m68ki_cpu_core* core, unsigned int address);
	unsigned int m68k_read_memory_32(m68ki_cpu_core* core, unsigned int address);
	void m68k_write_memory_8(m68ki_cpu_core* core, unsigned int address, unsigned int value);
	void m68k_write_memory_16(m68ki_cpu_core* core, unsigned int address, unsigned int value);
	void m68k_write_memory_32(m68ki_cpu_core* core, unsigned int address, unsigned int value);
	int read_sp_on_reset(m68ki_cpu_core* core);
	int read_pc_on_reset(m68ki_cpu_core* core);
}

#else

#if defined(_MSC_VER)
#  define MC68K_WEAK_NOINLINE __declspec(noinline)
#else
#  define MC68K_WEAK_NOINLINE __attribute__((weak, noinline))
#endif

extern "C"
{
	MC68K_WEAK_NOINLINE unsigned int m68k_read_immediate_16(m68ki_cpu_core* core, unsigned int address)
	{
		return mc68k::memoryOps::read<mc68k::Mc68k, uint16_t, true>(*mc68k_get_instance(core), address);
	}
	MC68K_WEAK_NOINLINE unsigned int m68k_read_immediate_32(m68ki_cpu_core* core, unsigned int address)
	{
		return mc68k::memoryOps::read<mc68k::Mc68k, uint32_t, true>(*mc68k_get_instance(core), address);
	}

	MC68K_WEAK_NOINLINE unsigned int m68k_read_pcrelative_8(m68ki_cpu_core* core, unsigned int address)
	{
		return mc68k::memoryOps::read<mc68k::Mc68k, uint8_t, false>(*mc68k_get_instance(core), address);
	}
	MC68K_WEAK_NOINLINE unsigned int m68k_read_pcrelative_16(m68ki_cpu_core* core, unsigned int address)
	{
		return mc68k::memoryOps::read<mc68k::Mc68k, uint16_t, false>(*mc68k_get_instance(core), address);
	}
	MC68K_WEAK_NOINLINE unsigned int m68k_read_pcrelative_32(m68ki_cpu_core* core, unsigned int address)
	{
		return mc68k::memoryOps::read<mc68k::Mc68k, uint32_t, false>(*mc68k_get_instance(core), address);
	}

	MC68K_WEAK_NOINLINE unsigned int m68k_read_memory_8(m68ki_cpu_core* core, unsigned int address)
	{
		return mc68k::memoryOps::read<mc68k::Mc68k, uint8_t, false>(*mc68k_get_instance(core), address);
	}
	MC68K_WEAK_NOINLINE unsigned int m68k_read_memory_16(m68ki_cpu_core* core, unsigned int address)
	{
		return mc68k::memoryOps::read<mc68k::Mc68k, uint16_t, false>(*mc68k_get_instance(core), address);
	}
	MC68K_WEAK_NOINLINE unsigned int m68k_read_memory_32(m68ki_cpu_core* core, unsigned int address)
	{
		return mc68k::memoryOps::read<mc68k::Mc68k, uint32_t, false>(*mc68k_get_instance(core), address);
	}
	MC68K_WEAK_NOINLINE void m68k_write_memory_8(m68ki_cpu_core* core, unsigned int address, unsigned int value)
	{
		mc68k::memoryOps::write<mc68k::Mc68k, uint8_t>(*mc68k_get_instance(core), address, static_cast<uint8_t>(value));
	}
	MC68K_WEAK_NOINLINE void m68k_write_memory_16(m68ki_cpu_core* core, unsigned int address, unsigned int value)
	{
		mc68k::memoryOps::write<mc68k::Mc68k, uint16_t>(*mc68k_get_instance(core), address, static_cast<uint16_t>(value));
	}
	MC68K_WEAK_NOINLINE void m68k_write_memory_32(m68ki_cpu_core* core, unsigned int address, unsigned int value)
	{
		mc68k::memoryOps::write<mc68k::Mc68k, uint32_t>(*mc68k_get_instance(core), address, value);
	}
	MC68K_WEAK_NOINLINE int read_sp_on_reset(m68ki_cpu_core* core)
	{
		return static_cast<int>(mc68k_get_instance(core)->getResetSP());
	}
	MC68K_WEAK_NOINLINE int read_pc_on_reset(m68ki_cpu_core* core)
	{
		return static_cast<int>(mc68k_get_instance(core)->getResetPC());
	}
}

#endif
