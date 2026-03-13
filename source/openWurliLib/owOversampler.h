/**
 * OpenWurli DSP — 2x polyphase IIR half-band oversampler
 * Ported from Rust openwurli-dsp (GPL v3)
 */
#pragma once

#include <vector>
#include <cstddef>

namespace openWurli
{

class AllpassSection
{
public:
	AllpassSection(double a = 0.0) : m_a(a), m_state(0.0) {}

	double process(double x)
	{
		const double y = m_a * x + m_state;
		m_state = x - m_a * y;
		return y;
	}

	void reset() { m_state = 0.0; }

private:
	double m_a;
	double m_state;
};

class AllpassBranch
{
public:
	AllpassBranch() = default;

	void init(const double* coeffs, int count)
	{
		m_sections.clear();
		for (int i = 0; i < count; i++)
			m_sections.emplace_back(coeffs[i]);
	}

	double process(double x)
	{
		double y = x;
		for (auto& s : m_sections)
			y = s.process(y);
		return y;
	}

	void reset()
	{
		for (auto& s : m_sections)
			s.reset();
	}

private:
	std::vector<AllpassSection> m_sections;
};

class Oversampler
{
public:
	Oversampler() = default;

	void init()
	{
		static constexpr double branchA[] = {0.036681502163648, 0.248030921580110, 0.643184620136480};
		static constexpr double branchB[] = {0.110377634768680, 0.420399304190880, 0.854640112701920};

		m_upA.init(branchA, 3);
		m_upB.init(branchB, 3);
		m_downA.init(branchA, 3);
		m_downB.init(branchB, 3);
		m_downDelay = 0.0;
	}

	void upsample2x(const double* input, double* output, size_t inputLen)
	{
		for (size_t i = 0; i < inputLen; i++)
		{
			const double x = input[i];
			const double a = m_upA.process(x);
			const double b = m_upB.process(x);
			output[i * 2] = a;
			output[i * 2 + 1] = b;
		}
	}

	void downsample2x(const double* input, double* output, size_t outputLen)
	{
		for (size_t i = 0; i < outputLen; i++)
		{
			const double a = m_downA.process(input[i * 2]);
			const double b = m_downB.process(input[i * 2 + 1]);
			output[i] = (a + m_downDelay) * 0.5;
			m_downDelay = b;
		}
	}

	void reset()
	{
		m_upA.reset();
		m_upB.reset();
		m_downA.reset();
		m_downB.reset();
		m_downDelay = 0.0;
	}

private:
	AllpassBranch m_upA, m_upB;
	AllpassBranch m_downA, m_downB;
	double m_downDelay = 0.0;
};

} // namespace openWurli
