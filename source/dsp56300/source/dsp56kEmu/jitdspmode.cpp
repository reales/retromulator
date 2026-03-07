#include "jitdspmode.h"

#include "dsp.h"

namespace dsp56k
{
	constexpr uint32_t g_aguShift = 1;

	void JitDspMode::initialize(const DSP& _dsp)
	{
		m_mode = 0;

		for(size_t i=0; i<8; ++i)
		{
			const auto m = calcAddressingMode(_dsp.regs().m[i]);

			m_mode |= static_cast<uint32_t>(m) << (i << g_aguShift);
		}

		// only the upper 16 bits of the SR are used, the 8 LSBs (CCR) are ignored
		auto sr = _dsp.regs().sr.toWord() & 0xffff00;

		// furthermore, ignore SR bits that the code doesn't depend on or evaluates at runtime
		sr &= SrModeChangeRelevantBits;

		m_mode |= sr << 8;
	}

	AddressingMode JitDspMode::getAddressingMode(const uint32_t _aguIndex) const
	{
		return static_cast<AddressingMode>((m_mode >> (_aguIndex << g_aguShift)) & 0x3);
	}

	uint32_t JitDspMode::getSR() const
	{
		return (m_mode >> 8) & 0xffff00;
	}

	uint32_t JitDspMode::testSR(const SRBit _bit) const
	{
		const auto sr = getSR();
		return sr & (1<<_bit);
	}

	namespace
	{
		constexpr AddressingMode calcAddressingMode(const uint32_t _m24)
		{
			const uint16_t m16 = _m24 & 0xffff;

			// Compute a 2-bit index directly, matching our enum values
		    uint32_t index = ((static_cast<uint16_t>(m16 + 1) <= 1) << 1) | (m16 >> 15);

		    const auto mode = static_cast<AddressingMode>(index);
			return mode;
		}

		static_assert(calcAddressingMode(0x0000) == AddressingMode::Bitreverse);
		static_assert(calcAddressingMode(0x0001) == AddressingMode::Modulo);
		static_assert(calcAddressingMode(0x7fff) == AddressingMode::Modulo);
		static_assert(calcAddressingMode(0x8000) == AddressingMode::MultiWrapModulo);
		static_assert(calcAddressingMode(0x8001) == AddressingMode::MultiWrapModulo);
		static_assert(calcAddressingMode(0xfffe) == AddressingMode::MultiWrapModulo);
		static_assert(calcAddressingMode(0xffff) == AddressingMode::Linear);
	}

	AddressingMode JitDspMode::calcAddressingMode(const TReg24& _m)
	{
		return dsp56k::calcAddressingMode(_m.toWord());
	}
}
