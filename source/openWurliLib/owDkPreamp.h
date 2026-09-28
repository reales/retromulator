/**
 * OpenWurli DSP — DK (Discretization-Kernel) preamp, drawn-topology revision
 * Ported from Rust openwurli-dsp 0.9.0 dk_preamp_legacy.rs (GPL v3)
 *
 * Full coupled 2-stage BJT circuit solver: 9-node MNA, trapezoidal
 * discretization, Newton-Raphson on the 2×2 nonlinear kernel. R_ldr is an
 * explicit source term corrected via Sherman-Morrison on a fixed inverse.
 */
#pragma once

#include <cmath>
#include <algorithm>
#include <array>

namespace openWurli
{

static constexpr int DK_N = 9;

using Mat9 = std::array<std::array<double, DK_N>, DK_N>;
using Vec9 = std::array<double, DK_N>;

// Circuit constants
static constexpr double DK_VCC = 14.5;
static constexpr double DK_R1 = 22000.0;
static constexpr double DK_R2 = 1000000.0;
static constexpr double DK_R3 = 470000.0;
static constexpr double DK_RE1 = 33000.0;
static constexpr double DK_RC1 = 150000.0;
static constexpr double DK_RE2A = 270.0;
static constexpr double DK_RE2B = 820.0;
static constexpr double DK_RC2 = 1800.0;
static constexpr double DK_R9 = 6800.0;
static constexpr double DK_R10 = 56000.0;
static constexpr double DK_RLOAD = 100000.0;
static constexpr double DK_R_IN_EFF = DK_R1 * DK_R2 / (DK_R1 + DK_R2);
static constexpr double DK_K_IN_DIV = DK_R2 / (DK_R1 + DK_R2);
static constexpr double DK_CIN = 0.022e-6;
static constexpr double DK_C2 = 220.0e-12;
static constexpr double DK_C3 = 100.0e-12;
static constexpr double DK_C4 = 100.0e-12;
static constexpr double DK_CE1 = 4.7e-6;
static constexpr double DK_CE2 = 22.0e-6;
static constexpr double DK_C6 = 4.7e-6;

// 2N5089 card (forward-active, with base current and high injection)
static constexpr double DK_IS = 3.03e-14;
static constexpr double DK_VT = 0.026;
static constexpr double DK_NF = 1.005;
static constexpr double DK_BF = 1434.0;
static constexpr double DK_ISE = 2.88e-15;
static constexpr double DK_NE = 1.262;
static constexpr double DK_IKF = 0.01358;
static constexpr double DK_VTF = DK_NF * DK_VT;
static constexpr double DK_NE_VT = DK_NE * DK_VT;
static constexpr double DK_IS_OVER_IKF = DK_IS / DK_IKF;
static constexpr double DK_VBE_MAX = 0.85;

// Node indices
static constexpr int DK_BASE1 = 0, DK_EMIT1 = 1, DK_COLL1 = 2;
static constexpr int DK_EMIT2 = 3, DK_EMIT2B = 4, DK_COLL2 = 5;
static constexpr int DK_NODE_C6 = 6, DK_OUT = 7, DK_FB = 8;

struct DkIncidence { int node; double coeff; };

// vbe1 = v[BASE1] - v[EMIT1], vbe2 = v[COLL1] - v[EMIT2]
static constexpr DkIncidence DK_NV[2][2] = {
	{{DK_BASE1, 1.0}, {DK_EMIT1, -1.0}},
	{{DK_COLL1, 1.0}, {DK_EMIT2, -1.0}},
};
// Collector current: into the emitter node, out of the collector node
static constexpr DkIncidence DK_NIC[2][2] = {
	{{DK_EMIT1, 1.0}, {DK_COLL1, -1.0}},
	{{DK_EMIT2, 1.0}, {DK_COLL2, -1.0}},
};
// Base current: into the emitter node, out of the base node (TR-2's base is COLL1)
static constexpr DkIncidence DK_NIB[2][2] = {
	{{DK_EMIT1, 1.0}, {DK_BASE1, -1.0}},
	{{DK_EMIT2, 1.0}, {DK_COLL1, -1.0}},
};

inline Mat9 mat9Zero()
{
	Mat9 m;
	for (auto& row : m) row.fill(0.0);
	return m;
}

inline Vec9 vec9Zero()
{
	Vec9 v;
	v.fill(0.0);
	return v;
}

inline Vec9 matVecMul(const Mat9& a, const Vec9& x)
{
	Vec9 y;
	for (int i = 0; i < DK_N; i++)
	{
		double sum = 0.0;
		for (int j = 0; j < DK_N; j++)
			sum += a[i][j] * x[j];
		y[i] = sum;
	}
	return y;
}

inline Mat9 matAdd(const Mat9& a, const Mat9& b)
{
	Mat9 c;
	for (int i = 0; i < DK_N; i++)
		for (int j = 0; j < DK_N; j++)
			c[i][j] = a[i][j] + b[i][j];
	return c;
}

inline Mat9 matSub(const Mat9& a, const Mat9& b)
{
	Mat9 c;
	for (int i = 0; i < DK_N; i++)
		for (int j = 0; j < DK_N; j++)
			c[i][j] = a[i][j] - b[i][j];
	return c;
}

inline Mat9 matScale(double s, const Mat9& a)
{
	Mat9 b;
	for (int i = 0; i < DK_N; i++)
		for (int j = 0; j < DK_N; j++)
			b[i][j] = s * a[i][j];
	return b;
}

/// Gauss-Jordan inverse with partial pivoting.
inline Mat9 matInverse(const Mat9& m)
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
		{
			for (int j = 0; j < DK_N * 2; j++)
				std::swap(aug[col][j], aug[maxRow][j]);
		}
		const double pivot = aug[col][col];
		if (std::abs(pivot) < 1e-300)
			continue;
		for (int j = 0; j < DK_N * 2; j++)
			aug[col][j] /= pivot;
		for (int row = 0; row < DK_N; row++)
		{
			if (row == col) continue;
			const double factor = aug[row][col];
			if (factor == 0.0) continue;
			for (int j = 0; j < DK_N * 2; j++)
				aug[row][j] -= factor * aug[col][j];
		}
	}

	Mat9 inv;
	for (int i = 0; i < DK_N; i++)
		for (int j = 0; j < DK_N; j++)
			inv[i][j] = aug[i][DK_N + j];
	return inv;
}

inline void stampResistor(Mat9& g, int i, int j, double r)
{
	const double cond = 1.0 / r;
	g[i][i] += cond;
	g[j][j] += cond;
	g[i][j] -= cond;
	g[j][i] -= cond;
}

inline void stampCapacitor(Mat9& c, int i, int j, double cap)
{
	c[i][i] += cap;
	c[j][j] += cap;
	c[i][j] -= cap;
	c[j][i] -= cap;
}

inline void stampCapacitorToGnd(Mat9& c, int i, double cap)
{
	c[i][i] += cap;
}

struct DkBjt { double ic, ib, gic, gib; };

/// 2N5089 forward-active model: Ic (Gummel-Poon qb), Ib (ideal + recombination), derivatives.
inline DkBjt dkBjt(double vbe)
{
	const double v = std::clamp(vbe, -1.0, DK_VBE_MAX);
	const double ef = std::exp(v / DK_VTF);
	const double icc = DK_IS * (ef - 1.0);

	const double q2 = DK_IS_OVER_IKF * ef;
	const double root = std::sqrt(1.0 + 4.0 * q2);
	const double qb = 0.5 * (1.0 + root);
	const double ic = icc / qb;

	const double ee = std::exp(v / DK_NE_VT);
	const double ib = icc / DK_BF + DK_ISE * (ee - 1.0);

	const double dicc = DK_IS * ef / DK_VTF;
	const double dq2 = q2 / DK_VTF;
	const double dqb = dq2 / root;
	const double gic = (dicc * qb - icc * dqb) / (qb * qb);
	const double gib = dicc / DK_BF + DK_ISE * ee / DK_NE_VT;

	return {ic, ib, gic, gib};
}

/// K = N_v * S * N_i
inline void computeK(const Mat9& s, const DkIncidence (&ni)[2][2], double k[2][2])
{
	for (int i = 0; i < 2; i++)
	{
		for (int j = 0; j < 2; j++)
		{
			double acc = 0.0;
			for (const auto& nv : DK_NV[i])
				for (const auto& nc : ni[j])
					acc += nv.coeff * nc.coeff * s[nv.node][nc.node];
			k[i][j] = acc;
		}
	}
}

/// S * N_i[:, j] for one current column
inline Vec9 sTimesNi(const Mat9& s, const DkIncidence (&ni)[2])
{
	Vec9 out;
	for (int i = 0; i < DK_N; i++)
		out[i] = ni[0].coeff * s[i][ni[0].node] + ni[1].coeff * s[i][ni[1].node];
	return out;
}

struct DkState
{
	double jCin = 0.0;
	double cinRhsPrev = 0.0;
	Vec9 v{};
	double iC[2] = {0.0, 0.0};
	double iB[2] = {0.0, 0.0};
	double vNl[2] = {0.0, 0.0};

	static DkState atDc(double gCin, const double vNlDc[2], const Vec9& vDc)
	{
		DkState s;
		const auto d0 = dkBjt(vNlDc[0]);
		const auto d1 = dkBjt(vNlDc[1]);
		s.jCin = gCin * vDc[DK_BASE1];
		s.cinRhsPrev = gCin * vDc[DK_BASE1];
		s.v = vDc;
		s.iC[0] = d0.ic; s.iC[1] = d1.ic;
		s.iB[0] = d0.ib; s.iB[1] = d1.ib;
		s.vNl[0] = vNlDc[0]; s.vNl[1] = vNlDc[1];
		return s;
	}
};

class DkPreamp
{
public:
	DkPreamp() = default;

	void init(double sampleRate)
	{
		const double t = 1.0 / sampleRate;
		const double twoOverT = 2.0 / t;

		// Cin companion: Thevenin of the R1/R2/Cin input two-port seen from base1
		const double alphaCin = 2.0 * DK_R_IN_EFF * DK_CIN * sampleRate;
		m_gCin = (2.0 * DK_CIN * sampleRate) / (1.0 + alphaCin);
		m_cCin = (1.0 - alphaCin) / (1.0 + alphaCin);
		m_gc1pc = m_gCin * (1.0 + m_cCin);

		Mat9 gBase = mat9Zero();
		Vec9 w = vec9Zero();

		stampResistor(gBase, DK_BASE1, DK_EMIT2B, DK_R3);
		gBase[DK_EMIT1][DK_EMIT1] += 1.0 / DK_RE1;
		gBase[DK_COLL1][DK_COLL1] += 1.0 / DK_RC1;
		w[DK_COLL1] += DK_VCC / DK_RC1;
		stampResistor(gBase, DK_EMIT2, DK_EMIT2B, DK_RE2A);
		gBase[DK_EMIT2B][DK_EMIT2B] += 1.0 / DK_RE2B;
		gBase[DK_COLL2][DK_COLL2] += 1.0 / DK_RC2;
		w[DK_COLL2] += DK_VCC / DK_RC2;
		stampResistor(gBase, DK_NODE_C6, DK_OUT, DK_R9);
		stampResistor(gBase, DK_NODE_C6, DK_FB, DK_R10);
		gBase[DK_OUT][DK_OUT] += 1.0 / DK_RLOAD;

		m_gDcBase = gBase;
		gBase[DK_BASE1][DK_BASE1] += m_gCin;

		Mat9 c = mat9Zero();
		stampCapacitor(c, DK_COLL1, DK_BASE1, DK_C3);
		stampCapacitor(c, DK_COLL2, DK_COLL1, DK_C4);
		stampCapacitor(c, DK_EMIT1, DK_FB, DK_CE1);
		stampCapacitor(c, DK_COLL2, DK_NODE_C6, DK_C6);
		stampCapacitorToGnd(c, DK_EMIT2, DK_CE2);
		stampCapacitorToGnd(c, DK_BASE1, DK_C2);
		const Mat9 twoCOverT = matScale(twoOverT, c);

		for (int i = 0; i < DK_N; i++)
			m_twoW[i] = 2.0 * w[i];

		const Mat9 aBase = matAdd(twoCOverT, gBase);
		m_aNegBase = matSub(twoCOverT, gBase);
		m_sBase = matInverse(aBase);
		computeK(m_sBase, DK_NIC, m_kC);
		computeK(m_sBase, DK_NIB, m_kB);

		for (int i = 0; i < DK_N; i++)
		{
			m_sFbCol[i] = m_sBase[i][DK_FB];
			m_sFbRow[i] = m_sBase[DK_FB][i];
		}
		m_sFbFb = m_sBase[DK_FB][DK_FB];

		for (int i = 0; i < 2; i++)
		{
			m_nvSfb[i] = DK_NV[i][0].coeff * m_sFbCol[DK_NV[i][0].node]
			           + DK_NV[i][1].coeff * m_sFbCol[DK_NV[i][1].node];
			m_sfbNic[i] = DK_NIC[i][0].coeff * m_sFbRow[DK_NIC[i][0].node]
			            + DK_NIC[i][1].coeff * m_sFbRow[DK_NIC[i][1].node];
			m_sfbNib[i] = DK_NIB[i][0].coeff * m_sFbRow[DK_NIB[i][0].node]
			            + DK_NIB[i][1].coeff * m_sFbRow[DK_NIB[i][1].node];
			m_sNic[i] = sTimesNi(m_sBase, DK_NIC[i]);
			m_sNib[i] = sTimesNi(m_sBase, DK_NIB[i]);
		}

		m_rLdr = 1000000.0;
		m_gLdr = 1.0 / m_rLdr;
		m_gLdrPrev = m_gLdr;

		double vNlDc[2];
		Vec9 vDc;
		fullDcSolve(m_gDcBase, w, m_rLdr, vNlDc, vDc);
		m_vDc = vDc;
		m_main = DkState::atDc(m_gCin, vNlDc, vDc);
	}

	/// Factor turning `out` into the preamp's open-circuit voltage at the R-9 terminal.
	static constexpr double openCircuitOutputFactor()
	{
		return (DK_R9 + DK_RLOAD) / DK_RLOAD;
	}

	double processSample(double input)
	{
		const double out = dkStep(m_main, input);
		m_gLdrPrev = m_gLdr;

		if (!std::isfinite(out))
		{
			reset();
			return 0.0;
		}
		return out;
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
		Vec9 w;
		for (int i = 0; i < DK_N; i++)
			w[i] = m_twoW[i] * 0.5;

		double vNlDc[2];
		Vec9 vDc;
		fullDcSolve(m_gDcBase, w, m_rLdr, vNlDc, vDc);

		m_vDc = vDc;
		m_gLdr = 1.0 / m_rLdr;
		m_gLdrPrev = m_gLdr;
		m_main = DkState::atDc(m_gCin, vNlDc, vDc);
	}

private:
	static void fullDcSolve(const Mat9& gDcBase, const Vec9& w, double rLdr,
	                        double vNlDc[2], Vec9& vDc)
	{
		Mat9 gFull = gDcBase;
		gFull[DK_FB][DK_FB] += 1.0 / rLdr;
		const Mat9 sDc = matInverse(gFull);
		double kCDc[2][2], kBDc[2][2];
		computeK(sDc, DK_NIC, kCDc);
		computeK(sDc, DK_NIB, kBDc);
		const Vec9 sv = matVecMul(sDc, w);
		const double pDc[2] = { sv[DK_BASE1] - sv[DK_EMIT1], sv[DK_COLL1] - sv[DK_EMIT2] };

		double vNl0 = 0.56, vNl1 = 0.66;
		for (int iter = 0; iter < 200; iter++)
		{
			const auto d0 = dkBjt(vNl0);
			const auto d1 = dkBjt(vNl1);
			const double f0 = vNl0 - pDc[0]
				- kCDc[0][0] * d0.ic - kCDc[0][1] * d1.ic
				- kBDc[0][0] * d0.ib - kBDc[0][1] * d1.ib;
			const double f1 = vNl1 - pDc[1]
				- kCDc[1][0] * d0.ic - kCDc[1][1] * d1.ic
				- kBDc[1][0] * d0.ib - kBDc[1][1] * d1.ib;
			if (std::abs(f0) < 1e-13 && std::abs(f1) < 1e-13)
				break;

			const double j00 = 1.0 - kCDc[0][0] * d0.gic - kBDc[0][0] * d0.gib;
			const double j01 = -kCDc[0][1] * d1.gic - kBDc[0][1] * d1.gib;
			const double j10 = -kCDc[1][0] * d0.gic - kBDc[1][0] * d0.gib;
			const double j11 = 1.0 - kCDc[1][1] * d1.gic - kBDc[1][1] * d1.gib;
			const double det = j00 * j11 - j01 * j10;
			const double invDet = 1.0 / det;
			const double dv0 = invDet * (j11 * f0 - j01 * f1);
			const double dv1 = invDet * (j00 * f1 - j10 * f0);
			const double maxStep = 2.0 * DK_VT;
			vNl0 -= std::clamp(dv0, -maxStep, maxStep);
			vNl1 -= std::clamp(dv1, -maxStep, maxStep);
		}

		const auto d0 = dkBjt(vNl0);
		const auto d1 = dkBjt(vNl1);
		const double ic[2] = { d0.ic, d1.ic };
		const double ib[2] = { d0.ib, d1.ib };
		Vec9 dcRhs = w;
		for (int j = 0; j < 2; j++)
		{
			for (const auto& n : DK_NIC[j]) dcRhs[n.node] += n.coeff * ic[j];
			for (const auto& n : DK_NIB[j]) dcRhs[n.node] += n.coeff * ib[j];
		}
		vDc = matVecMul(sDc, dcRhs);
		vNlDc[0] = vNl0;
		vNlDc[1] = vNl1;
	}

	/// One trapezoidal DK step. Returns v[OUT].
	double dkStep(DkState& state, double input) const
	{
		// 1. History
		Vec9 rhs = matVecMul(m_aNegBase, state.v);
		rhs[DK_FB] -= m_gLdrPrev * state.v[DK_FB];

		const double vinEff = input * DK_K_IN_DIV;
		const double cinRhsNow = m_gCin * vinEff + state.jCin;
		rhs[DK_BASE1] += cinRhsNow + state.cinRhsPrev;

		for (int j = 0; j < 2; j++)
		{
			for (const auto& n : DK_NIC[j]) rhs[n.node] += n.coeff * state.iC[j];
			for (const auto& n : DK_NIB[j]) rhs[n.node] += n.coeff * state.iB[j];
		}
		for (int i = 0; i < DK_N; i++)
			rhs[i] += m_twoW[i];

		// 2. Prediction without R_ldr on the LHS
		const Vec9 vPredBase = matVecMul(m_sBase, rhs);

		// 3. Sherman-Morrison correction for the current R_ldr
		const double smK = m_gLdr / (1.0 + m_sFbFb * m_gLdr);
		const double smVpred = smK * vPredBase[DK_FB];
		Vec9 vPred;
		for (int i = 0; i < DK_N; i++)
			vPred[i] = vPredBase[i] - smVpred * m_sFbCol[i];

		// 4. Predicted NL voltages
		const double p0 = vPred[DK_BASE1] - vPred[DK_EMIT1];
		const double p1 = vPred[DK_COLL1] - vPred[DK_EMIT2];

		// 5. NR on the 2x2 system with R_ldr-corrected kernels
		double kc[2][2], kb[2][2];
		for (int i = 0; i < 2; i++)
		{
			for (int j = 0; j < 2; j++)
			{
				kc[i][j] = m_kC[i][j] - smK * m_nvSfb[i] * m_sfbNic[j];
				kb[i][j] = m_kB[i][j] - smK * m_nvSfb[i] * m_sfbNib[j];
			}
		}

		double vNl0 = state.vNl[0], vNl1 = state.vNl[1];
		for (int iter = 0; iter < 6; iter++)
		{
			const auto d0 = dkBjt(vNl0);
			const auto d1 = dkBjt(vNl1);

			const double f0 = vNl0 - p0 - kc[0][0] * d0.ic - kc[0][1] * d1.ic - kb[0][0] * d0.ib - kb[0][1] * d1.ib;
			const double f1 = vNl1 - p1 - kc[1][0] * d0.ic - kc[1][1] * d1.ic - kb[1][0] * d0.ib - kb[1][1] * d1.ib;

			if (std::abs(f0) < 1e-9 && std::abs(f1) < 1e-9)
				break;

			const double j00 = 1.0 - kc[0][0] * d0.gic - kb[0][0] * d0.gib;
			const double j01 = -kc[0][1] * d1.gic - kb[0][1] * d1.gib;
			const double j10 = -kc[1][0] * d0.gic - kb[1][0] * d0.gib;
			const double j11 = 1.0 - kc[1][1] * d1.gic - kb[1][1] * d1.gib;

			const double det = j00 * j11 - j01 * j10;
			if (std::abs(det) < 1e-30)
				break;
			const double invDet = 1.0 / det;

			vNl0 -= invDet * (j11 * f0 - j01 * f1);
			vNl1 -= invDet * (j00 * f1 - j10 * f0);
		}

		// 6. Final NL currents
		const auto d0 = dkBjt(vNl0);
		const auto d1 = dkBjt(vNl1);
		const double icNew[2] = { d0.ic, d1.ic };
		const double ibNew[2] = { d0.ib, d1.ib };

		// 7. Node voltage update
		const double sfbDot = m_sfbNic[0] * icNew[0] + m_sfbNic[1] * icNew[1]
		                    + m_sfbNib[0] * ibNew[0] + m_sfbNib[1] * ibNew[1];
		for (int i = 0; i < DK_N; i++)
		{
			const double sNiI = icNew[0] * m_sNic[0][i] + icNew[1] * m_sNic[1][i]
			                  + ibNew[0] * m_sNib[0][i] + ibNew[1] * m_sNib[1][i];
			state.v[i] = vPred[i] + sNiI - smK * sfbDot * m_sFbCol[i];
		}

		// 8. Cin companion update
		state.cinRhsPrev = cinRhsNow;
		const double dvCin = vinEff - state.v[DK_BASE1];
		state.jCin = -m_gc1pc * dvCin - m_cCin * state.jCin;

		// 9. State update
		state.iC[0] = icNew[0]; state.iC[1] = icNew[1];
		state.iB[0] = ibNew[0]; state.iB[1] = ibNew[1];
		state.vNl[0] = vNl0; state.vNl[1] = vNl1;

		return state.v[DK_OUT];
	}

	Mat9 m_sBase{};
	Mat9 m_aNegBase{};
	double m_kC[2][2] = {};
	double m_kB[2][2] = {};
	Vec9 m_twoW{};
	Vec9 m_sNic[2]{};
	Vec9 m_sNib[2]{};
	Vec9 m_sFbCol{};
	Vec9 m_sFbRow{};
	double m_sFbFb = 0.0;
	double m_nvSfb[2] = {0.0, 0.0};
	double m_sfbNic[2] = {0.0, 0.0};
	double m_sfbNib[2] = {0.0, 0.0};
	Vec9 m_vDc{};
	Mat9 m_gDcBase{};
	double m_gCin = 0.0, m_cCin = 0.0, m_gc1pc = 0.0;
	DkState m_main;
	double m_rLdr = 1000000.0;
	double m_gLdr = 1e-6;
	double m_gLdrPrev = 1e-6;
};

} // namespace openWurli
