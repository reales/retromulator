/**
 * OpenWurli DSP — DK (Discretization-Kernel) preamp
 * Ported from Rust openwurli-dsp (GPL v3)
 *
 * Full coupled 2-stage BJT circuit solver with 8-node MNA,
 * trapezoidal discretization, Newton-Raphson on 2×2 nonlinear kernel.
 * Shadow preamp for pump cancellation.
 */
#pragma once

#include <cmath>
#include <algorithm>
#include <array>
#include <cstring>

namespace openWurli
{

static constexpr int DK_N = 8;

using Mat8 = std::array<std::array<double, DK_N>, DK_N>;
using Vec8 = std::array<double, DK_N>;

// Circuit constants
static constexpr double DK_VCC = 15.0;
static constexpr double DK_R1 = 22000.0;
static constexpr double DK_R2 = 2000000.0;
static constexpr double DK_R3 = 470000.0;
static constexpr double DK_RE1 = 33000.0;
static constexpr double DK_RC1 = 150000.0;
static constexpr double DK_RE2A = 270.0;
static constexpr double DK_RE2B = 820.0;
static constexpr double DK_RC2 = 1800.0;
static constexpr double DK_R9 = 6800.0;
static constexpr double DK_R10 = 56000.0;
static constexpr double DK_CIN = 0.022e-6;
static constexpr double DK_C3 = 100.0e-12;
static constexpr double DK_C4 = 100.0e-12;
static constexpr double DK_CE1 = 4.7e-6;
static constexpr double DK_CE2 = 22.0e-6;
static constexpr double DK_IS = 3.03e-14;
static constexpr double DK_VT = 0.026;
static constexpr double DK_IS_OVER_VT = DK_IS / DK_VT;
static constexpr double DK_VBE_MAX = 0.85;

// Node indices
static constexpr int BASE1 = 0, EMIT1 = 1, COLL1 = 2;
static constexpr int EMIT2 = 3, EMIT2B = 4, COLL2 = 5;
static constexpr int OUT = 6, FB = 7;

// Matrix helpers
inline Mat8 mat8Zero()
{
	Mat8 m;
	for (auto& row : m) row.fill(0.0);
	return m;
}

inline Vec8 vec8Zero()
{
	Vec8 v;
	v.fill(0.0);
	return v;
}

inline Vec8 matVecMul(const Mat8& a, const Vec8& x)
{
	Vec8 y = vec8Zero();
	for (int i = 0; i < DK_N; i++)
	{
		double sum = 0.0;
		for (int j = 0; j < DK_N; j++)
			sum += a[i][j] * x[j];
		y[i] = sum;
	}
	return y;
}

inline Mat8 matAdd(const Mat8& a, const Mat8& b)
{
	Mat8 c;
	for (int i = 0; i < DK_N; i++)
		for (int j = 0; j < DK_N; j++)
			c[i][j] = a[i][j] + b[i][j];
	return c;
}

inline Mat8 matSub(const Mat8& a, const Mat8& b)
{
	Mat8 c;
	for (int i = 0; i < DK_N; i++)
		for (int j = 0; j < DK_N; j++)
			c[i][j] = a[i][j] - b[i][j];
	return c;
}

inline Mat8 matScale(double s, const Mat8& a)
{
	Mat8 b;
	for (int i = 0; i < DK_N; i++)
		for (int j = 0; j < DK_N; j++)
			b[i][j] = s * a[i][j];
	return b;
}

inline Mat8 matInverse(const Mat8& m)
{
	double aug[DK_N][DK_N * 2];
	for (int i = 0; i < DK_N; i++)
	{
		for (int j = 0; j < DK_N; j++)
		{
			aug[i][j] = m[i][j];
			aug[i][DK_N + j] = (i == j) ? 1.0 : 0.0;
		}
	}

	for (int col = 0; col < DK_N; col++)
	{
		double maxVal = std::abs(aug[col][col]);
		int maxRow = col;
		for (int row = col + 1; row < DK_N; row++)
		{
			if (std::abs(aug[row][col]) > maxVal)
			{
				maxVal = std::abs(aug[row][col]);
				maxRow = row;
			}
		}
		if (maxRow != col)
			std::swap_ranges(&aug[col][0], &aug[col][DK_N * 2], &aug[maxRow][0]);

		const double pivot = aug[col][col];
		for (int j = 0; j < DK_N * 2; j++)
			aug[col][j] /= pivot;

		for (int row = 0; row < DK_N; row++)
		{
			if (row != col)
			{
				const double factor = aug[row][col];
				for (int j = 0; j < DK_N * 2; j++)
					aug[row][j] -= factor * aug[col][j];
			}
		}
	}

	Mat8 inv;
	for (int i = 0; i < DK_N; i++)
		for (int j = 0; j < DK_N; j++)
			inv[i][j] = aug[i][DK_N + j];
	return inv;
}

inline void stampResistor(Mat8& g, int i, int j, double r)
{
	const double cond = 1.0 / r;
	g[i][i] += cond; g[j][j] += cond;
	g[i][j] -= cond; g[j][i] -= cond;
}

inline void stampCapacitor(Mat8& c, int i, int j, double cap)
{
	c[i][i] += cap; c[j][j] += cap;
	c[i][j] -= cap; c[j][i] -= cap;
}

// BJT model
inline double bjtIc(double vbe)
{
	const double v = std::clamp(vbe, -1.0, DK_VBE_MAX);
	return DK_IS * (std::exp(v / DK_VT) - 1.0);
}

inline void bjtIcGm(double vbe, double& ic, double& gm)
{
	const double v = std::clamp(vbe, -1.0, DK_VBE_MAX);
	const double expV = std::exp(v / DK_VT);
	ic = DK_IS * (expV - 1.0);
	gm = DK_IS_OVER_VT * expV;
}

using K2x2 = std::array<std::array<double, 2>, 2>;

inline K2x2 computeK(const Mat8& s)
{
	K2x2 k;
	k[0][0] = s[BASE1][EMIT1] - s[BASE1][COLL1] - s[EMIT1][EMIT1] + s[EMIT1][COLL1];
	k[0][1] = s[BASE1][EMIT2] - s[BASE1][COLL2] - s[EMIT1][EMIT2] + s[EMIT1][COLL2];
	k[1][0] = s[COLL1][EMIT1] - s[COLL1][COLL1] - s[EMIT2][EMIT1] + s[EMIT2][COLL1];
	k[1][1] = s[COLL1][EMIT2] - s[COLL1][COLL2] - s[EMIT2][EMIT2] + s[EMIT2][COLL2];
	return k;
}

struct DkState
{
	double jCin = 0.0;
	double cinRhsPrev = 0.0;
	Vec8 v;
	double iNl[2] = {0.0, 0.0};
	double vNl[2] = {0.0, 0.0};

	static DkState atDc(double gCin, const double vNlDc[2], const Vec8& vDc)
	{
		DkState s;
		s.jCin = gCin * vDc[BASE1];
		s.cinRhsPrev = gCin * vDc[BASE1];
		s.v = vDc;
		s.iNl[0] = bjtIc(vNlDc[0]);
		s.iNl[1] = bjtIc(vNlDc[1]);
		s.vNl[0] = vNlDc[0];
		s.vNl[1] = vNlDc[1];
		return s;
	}
};

/// Core DK trapezoidal step
inline double dkStep(
	const Mat8& aNegBase, const Vec8& twoW, const Mat8& sBase,
	const Vec8& sFbCol, double sFbFb,
	double gLdr, double gLdrPrev,
	const K2x2& k, const double nvSfb[2], const double sfbNi[2],
	double gCin, double gc1pc, double cCin,
	DkState& state, double input)
{
	// 1. History
	auto rhs = matVecMul(aNegBase, state.v);
	rhs[FB] -= gLdrPrev * state.v[FB];

	const double cinRhsNow = gCin * input + state.jCin;
	rhs[BASE1] += cinRhsNow + state.cinRhsPrev;

	rhs[EMIT1] += state.iNl[0]; rhs[COLL1] -= state.iNl[0];
	rhs[EMIT2] += state.iNl[1]; rhs[COLL2] -= state.iNl[1];

	for (int i = 0; i < DK_N; i++)
		rhs[i] += twoW[i];

	// 2. v_pred_base
	auto vPredBase = matVecMul(sBase, rhs);

	// 3. SM correction
	const double smK = gLdr / (1.0 + sFbFb * gLdr);
	const double smVpred = smK * vPredBase[FB];
	Vec8 vPred;
	for (int i = 0; i < DK_N; i++)
		vPred[i] = vPredBase[i] - smVpred * sFbCol[i];

	// 4. Predicted NL voltages
	const double p0 = vPred[BASE1] - vPred[EMIT1];
	const double p1 = vPred[COLL1] - vPred[EMIT2];

	// 5. NR solve with R_ldr-corrected K
	const double k00 = k[0][0] - smK * nvSfb[0] * sfbNi[0];
	const double k01 = k[0][1] - smK * nvSfb[0] * sfbNi[1];
	const double k10 = k[1][0] - smK * nvSfb[1] * sfbNi[0];
	const double k11 = k[1][1] - smK * nvSfb[1] * sfbNi[1];

	double vNl0 = state.vNl[0], vNl1 = state.vNl[1];

	for (int iter = 0; iter < 6; iter++)
	{
		double ic0, gm0, ic1, gm1;
		bjtIcGm(vNl0, ic0, gm0);
		bjtIcGm(vNl1, ic1, gm1);

		const double f0 = vNl0 - p0 - k00 * ic0 - k01 * ic1;
		const double f1 = vNl1 - p1 - k10 * ic0 - k11 * ic1;

		if (std::abs(f0) < 1e-9 && std::abs(f1) < 1e-9)
			break;

		const double j00 = 1.0 - k00 * gm0;
		const double j01 = -k01 * gm1;
		const double j10 = -k10 * gm0;
		const double j11 = 1.0 - k11 * gm1;

		const double det = j00 * j11 - j01 * j10;
		if (std::abs(det) < 1e-30) break;
		const double invDet = 1.0 / det;

		vNl0 -= invDet * (j11 * f0 - j01 * f1);
		vNl1 -= invDet * (j00 * f1 - j10 * f0);
	}

	// 6. Final NL currents
	const double icNew0 = bjtIc(vNl0);
	const double icNew1 = bjtIc(vNl1);

	// 7. Node voltage update
	const double sfbNiDotIc = sfbNi[0] * icNew0 + sfbNi[1] * icNew1;
	for (int i = 0; i < DK_N; i++)
	{
		const double sNi = icNew0 * (sBase[i][EMIT1] - sBase[i][COLL1])
						 + icNew1 * (sBase[i][EMIT2] - sBase[i][COLL2]);
		state.v[i] = vPred[i] + sNi - smK * sfbNiDotIc * sFbCol[i];
	}

	// 8. Cin companion update
	state.cinRhsPrev = cinRhsNow;
	const double dvCin = input - state.v[BASE1];
	state.jCin = -gc1pc * dvCin - cCin * state.jCin;

	// 9. State update
	state.iNl[0] = icNew0; state.iNl[1] = icNew1;
	state.vNl[0] = vNl0;   state.vNl[1] = vNl1;

	return state.v[OUT];
}

class DkPreamp
{
public:
	DkPreamp() = default;

	void init(double sampleRate)
	{
		const double t = 1.0 / sampleRate;
		const double twoOverT = 2.0 / t;

		// Cin-R1 companion
		const double alphaCin = 2.0 * DK_R1 * DK_CIN * sampleRate;
		m_gCin = (2.0 * DK_CIN * sampleRate) / (1.0 + alphaCin);
		m_cCin = (1.0 - alphaCin) / (1.0 + alphaCin);
		m_gc1pc = m_gCin * (1.0 + m_cCin);

		// G_base (no R_ldr)
		auto gBase = mat8Zero();
		auto w = vec8Zero();

		gBase[BASE1][BASE1] += 1.0 / DK_R2; w[BASE1] += DK_VCC / DK_R2;
		gBase[BASE1][BASE1] += 1.0 / DK_R3;
		gBase[EMIT1][EMIT1] += 1.0 / DK_RE1;
		gBase[COLL1][COLL1] += 1.0 / DK_RC1; w[COLL1] += DK_VCC / DK_RC1;
		stampResistor(gBase, EMIT2, EMIT2B, DK_RE2A);
		gBase[EMIT2B][EMIT2B] += 1.0 / DK_RE2B;
		gBase[COLL2][COLL2] += 1.0 / DK_RC2; w[COLL2] += DK_VCC / DK_RC2;
		stampResistor(gBase, COLL2, OUT, DK_R9);
		stampResistor(gBase, OUT, FB, DK_R10);

		m_gDcBase = gBase;

		// Add g_cin
		gBase[BASE1][BASE1] += m_gCin;

		// C matrix
		auto c = mat8Zero();
		stampCapacitor(c, COLL1, BASE1, DK_C3);
		stampCapacitor(c, COLL2, COLL1, DK_C4);
		stampCapacitor(c, EMIT1, FB, DK_CE1);
		stampCapacitor(c, EMIT2, EMIT2B, DK_CE2);
		auto twoCoverT = matScale(twoOverT, c);

		for (int i = 0; i < DK_N; i++)
			m_twoW[i] = 2.0 * w[i];

		auto aBase = matAdd(twoCoverT, gBase);
		m_aNegBase = matSub(twoCoverT, gBase);
		m_sBase = matInverse(aBase);
		m_k = computeK(m_sBase);

		// SM projection vectors
		for (int i = 0; i < DK_N; i++)
		{
			m_sFbCol[i] = m_sBase[i][FB];
			m_sFbRow[i] = m_sBase[FB][i];
		}
		m_sFbFb = m_sBase[FB][FB];

		m_nvSfb[0] = m_sFbCol[BASE1] - m_sFbCol[EMIT1];
		m_nvSfb[1] = m_sFbCol[COLL1] - m_sFbCol[EMIT2];
		m_sfbNi[0] = m_sFbRow[EMIT1] - m_sFbRow[COLL1];
		m_sfbNi[1] = m_sFbRow[EMIT2] - m_sFbRow[COLL2];

		// DC solve
		constexpr double rLdrInit = 1000000.0;
		double vNlDc[2]; Vec8 vDc;
		fullDcSolve(m_gDcBase, w, rLdrInit, vNlDc, vDc);

		m_vDc = vDc;
		m_main = DkState::atDc(m_gCin, vNlDc, vDc);
		m_shadow = m_main;

		m_rLdr = rLdrInit;
		m_gLdr = 1.0 / rLdrInit;
		m_gLdrPrev = m_gLdr;
	}

	double processSample(double input)
	{
		const double mainOut = dkStep(
			m_aNegBase, m_twoW, m_sBase, m_sFbCol, m_sFbFb,
			m_gLdr, m_gLdrPrev, m_k, m_nvSfb, m_sfbNi,
			m_gCin, m_gc1pc, m_cCin, m_main, input);

		const double pump = dkStep(
			m_aNegBase, m_twoW, m_sBase, m_sFbCol, m_sFbFb,
			m_gLdr, m_gLdrPrev, m_k, m_nvSfb, m_sfbNi,
			m_gCin, m_gc1pc, m_cCin, m_shadow, 0.0);

		m_gLdrPrev = m_gLdr;

		const double result = mainOut - pump;

		if (!std::isfinite(result))
		{
			reset();
			return 0.0;
		}
		return result;
	}

	void setLdrResistance(double rLdrPath)
	{
		const double newR = std::max(rLdrPath, 1000.0);
		if (std::abs(newR - m_rLdr) > 0.01)
		{
			m_rLdr = newR;
			m_gLdr = 1.0 / newR;
		}
	}

	void reset()
	{
		Vec8 wHalf;
		for (int i = 0; i < DK_N; i++)
			wHalf[i] = m_twoW[i] * 0.5;

		double vNlDc[2]; Vec8 vDc;
		fullDcSolve(m_gDcBase, wHalf, m_rLdr, vNlDc, vDc);

		m_vDc = vDc;
		m_gLdr = 1.0 / m_rLdr;
		m_gLdrPrev = m_gLdr;

		auto state = DkState::atDc(m_gCin, vNlDc, vDc);
		m_shadow = state;
		m_main = state;
	}

private:
	static void fullDcSolve(const Mat8& gDcBase, const Vec8& w, double rLdr,
							double vNlDc[2], Vec8& vDc)
	{
		auto gFull = gDcBase;
		gFull[FB][FB] += 1.0 / rLdr;
		auto sDc = matInverse(gFull);
		auto kDc = computeK(sDc);
		auto sv = matVecMul(sDc, w);
		const double pDc0 = sv[BASE1] - sv[EMIT1];
		const double pDc1 = sv[COLL1] - sv[EMIT2];

		double vNl0 = 0.56, vNl1 = 0.66;
		for (int iter = 0; iter < 100; iter++)
		{
			double ic0, gm0, ic1, gm1;
			bjtIcGm(vNl0, ic0, gm0);
			bjtIcGm(vNl1, ic1, gm1);

			const double f0 = vNl0 - pDc0 - kDc[0][0] * ic0 - kDc[0][1] * ic1;
			const double f1 = vNl1 - pDc1 - kDc[1][0] * ic0 - kDc[1][1] * ic1;

			if (std::abs(f0) < 1e-12 && std::abs(f1) < 1e-12) break;

			const double j00 = 1.0 - kDc[0][0] * gm0;
			const double j01 = -kDc[0][1] * gm1;
			const double j10 = -kDc[1][0] * gm0;
			const double j11 = 1.0 - kDc[1][1] * gm1;
			const double det = j00 * j11 - j01 * j10;
			const double invDet = 1.0 / det;
			const double dv0 = invDet * (j11 * f0 - j01 * f1);
			const double dv1 = invDet * (j00 * f1 - j10 * f0);
			constexpr double maxStep = 2.0 * DK_VT;
			vNl0 -= std::clamp(dv0, -maxStep, maxStep);
			vNl1 -= std::clamp(dv1, -maxStep, maxStep);
		}

		vNlDc[0] = vNl0; vNlDc[1] = vNl1;

		auto dcRhs = w;
		dcRhs[EMIT1] += bjtIc(vNl0); dcRhs[COLL1] -= bjtIc(vNl0);
		dcRhs[EMIT2] += bjtIc(vNl1); dcRhs[COLL2] -= bjtIc(vNl1);
		vDc = matVecMul(sDc, dcRhs);
	}

	Mat8 m_sBase, m_aNegBase, m_gDcBase;
	K2x2 m_k;
	Vec8 m_twoW, m_vDc;
	Vec8 m_sFbCol, m_sFbRow;
	double m_sFbFb = 0.0;
	double m_nvSfb[2] = {0.0, 0.0};
	double m_sfbNi[2] = {0.0, 0.0};
	double m_gCin = 0.0, m_cCin = 0.0, m_gc1pc = 0.0;
	DkState m_main, m_shadow;
	double m_rLdr = 1000000.0;
	double m_gLdr = 1e-6;
	double m_gLdrPrev = 1e-6;
};

} // namespace openWurli
