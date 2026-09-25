#pragma once

#include <array>

namespace matrixLib
{
	// 2x polyphase IIR decimator, after hiir by Laurent de Soras (WTFPL).
	// 8 coefficients, transition band 0.04: 99 dB stopband, passband up to 0.23 of the input rate.
	class Halfband2x
	{
	public:
		static constexpr int NC = 8;

		void reset()
		{
			for(auto& m : m_mem)
				m = 0.0f;
		}

		// _first is the earlier of the two input samples
		float process(const float _first, const float _second)
		{
			static constexpr float coefs[NC] =
			{
				0.04063346092419326f, 0.1505051290226746f, 0.30075705599187408f, 0.46077450496145034f,
				0.60952431489618808f, 0.73850384111885781f, 0.84922381039206596f, 0.94974278370500276f
			};

			float s0 = _second;
			float s1 = _first;
			for(int i = 0; i < NC; i += 2)
			{
				const int c = i + 2;
				const float t0 = (s0 - m_mem[c]) * coefs[i] + m_mem[c - 2];
				const float t1 = (s1 - m_mem[c + 1]) * coefs[i + 1] + m_mem[c - 1];
				m_mem[c - 2] = s0;
				m_mem[c - 1] = s1;
				s0 = t0;
				s1 = t1;
			}
			m_mem[NC] = s0;
			m_mem[NC + 1] = s1;
			return 0.5f * (s0 + s1);
		}

	private:
		std::array<float, NC + 2> m_mem{};
	};
}
