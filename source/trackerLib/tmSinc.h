#pragma once

// 512-tap Hann windowed sinc with a dynamic cutoff, shared by the FT2 and Schism mixers.
// The per-tap sin/cos are replaced by angle-sum tables, the tap loops are SIMD.

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(__aarch64__) || defined(_M_ARM64)
	#include <arm_neon.h>
	#define TM_SINC_NEON 1
#elif defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
	#include <emmintrin.h>
	#define TM_SINC_SSE2 1
#endif

#define TM_SINC_HALF 256
#define TM_SINC_SIZE (TM_SINC_HALF * 2 + 1)
#define TM_SINC_PAD (TM_SINC_SIZE + 3)
#define TM_SINC_PI 3.14159265358979323846

// One per mixing thread. Tables are indexed by tap + TM_SINC_HALF.
typedef struct tmSinc_t
{
	double cutoff; // of a/b
	double k[TM_SINC_PAD];
	double wc[TM_SINC_PAD], ws[TM_SINC_PAD]; // 0.5*cos(pi*k/256), 0.5*sin(pi*k/256)
	double a1[TM_SINC_PAD], b1[TM_SINC_PAD]; // cutoff 1
	double a[TM_SINC_PAD], b[TM_SINC_PAD]; // sin(c*pi*k)/pi, cos(c*pi*k)/pi
	double w[TM_SINC_PAD]; // tap weights of the current output sample
} tmSinc_t;

static inline void tmSincSetCutoff(double *a, double *b, const double cutoff)
{
	for (int32_t k = 0; k <= TM_SINC_HALF; k++)
	{
		const double s = sin(cutoff * TM_SINC_PI * k) * (1.0 / TM_SINC_PI);
		const double c = cos(cutoff * TM_SINC_PI * k) * (1.0 / TM_SINC_PI);
		a[TM_SINC_HALF + k] = s;
		a[TM_SINC_HALF - k] = -s;
		b[TM_SINC_HALF + k] = b[TM_SINC_HALF - k] = c;
	}

	// taps 0 and 1 sit next to the pole, they are computed directly
	a[TM_SINC_HALF] = b[TM_SINC_HALF] = 0.0;
	a[TM_SINC_HALF + 1] = b[TM_SINC_HALF + 1] = 0.0;
}

static inline tmSinc_t *tmSincCreate(void)
{
	tmSinc_t *t = (tmSinc_t *)calloc(1, sizeof (tmSinc_t));
	if (t == NULL)
		return NULL;

	for (int32_t i = 0; i < TM_SINC_PAD; i++)
	{
		const double k = (double)(i - TM_SINC_HALF);
		t->k[i] = k;
		t->wc[i] = 0.5 * cos(k * (TM_SINC_PI / TM_SINC_HALF));
		t->ws[i] = 0.5 * sin(k * (TM_SINC_PI / TM_SINC_HALF));
	}

	tmSincSetCutoff(t->a1, t->b1, 1.0);
	return t;
}

static inline double tmSincCutoff(double speed)
{
	speed = fabs(speed);
	return (speed > 1.0) ? 1.0 / speed : 1.0;
}

// fills t->w for the taps kmin..kmax around a position with the fraction f (0..1)
static inline void tmSincWeights(tmSinc_t *t, const int32_t kmin, const int32_t kmax, double f, const double cutoff)
{
	const double *a = t->a1, *b = t->b1;
	if (cutoff < 1.0)
	{
		if (cutoff != t->cutoff)
		{
			tmSincSetCutoff(t->a, t->b, cutoff);
			t->cutoff = cutoff;
		}
		a = t->a;
		b = t->b;
	}

	if (f <= 0.0)
		f = 1.0e-25;

	const double cb = cos(cutoff * TM_SINC_PI * f), sb = sin(cutoff * TM_SINC_PI * f);
	const double cw = cos(f * (TM_SINC_PI / TM_SINC_HALF)), sw = sin(f * (TM_SINC_PI / TM_SINC_HALF));

	const int32_t n = kmax - kmin + 1;
	const int32_t i0 = kmin + TM_SINC_HALF;
	const double *pk = t->k + i0, *pwc = t->wc + i0, *pws = t->ws + i0;
	double *w = t->w;
	a += i0;
	b += i0;

#if defined(TM_SINC_NEON)
	const float64x2_t vcb = vdupq_n_f64(cb), vsb = vdupq_n_f64(sb);
	const float64x2_t vcw = vdupq_n_f64(cw), vsw = vdupq_n_f64(sw);
	const float64x2_t vf = vdupq_n_f64(f), vhalf = vdupq_n_f64(0.5);
	for (int32_t j = 0; j < n; j += 2)
	{
		const float64x2_t num = vsubq_f64(vmulq_f64(vld1q_f64(a + j), vcb), vmulq_f64(vld1q_f64(b + j), vsb));
		const float64x2_t win = vaddq_f64(vhalf, vaddq_f64(vmulq_f64(vld1q_f64(pwc + j), vcw), vmulq_f64(vld1q_f64(pws + j), vsw)));
		vst1q_f64(w + j, vdivq_f64(vmulq_f64(num, win), vsubq_f64(vld1q_f64(pk + j), vf)));
	}
#elif defined(TM_SINC_SSE2)
	const __m128d vcb = _mm_set1_pd(cb), vsb = _mm_set1_pd(sb);
	const __m128d vcw = _mm_set1_pd(cw), vsw = _mm_set1_pd(sw);
	const __m128d vf = _mm_set1_pd(f), vhalf = _mm_set1_pd(0.5);
	for (int32_t j = 0; j < n; j += 2)
	{
		const __m128d num = _mm_sub_pd(_mm_mul_pd(_mm_loadu_pd(a + j), vcb), _mm_mul_pd(_mm_loadu_pd(b + j), vsb));
		const __m128d win = _mm_add_pd(vhalf, _mm_add_pd(_mm_mul_pd(_mm_loadu_pd(pwc + j), vcw), _mm_mul_pd(_mm_loadu_pd(pws + j), vsw)));
		_mm_storeu_pd(w + j, _mm_div_pd(_mm_mul_pd(num, win), _mm_sub_pd(_mm_loadu_pd(pk + j), vf)));
	}
#else
	for (int32_t j = 0; j < n; j++)
		w[j] = (a[j] * cb - b[j] * sb) * (0.5 + pwc[j] * cw + pws[j] * sw) / (pk[j] - f);
#endif

	for (int32_t k = 0; k <= 1; k++)
	{
		if (k < kmin || k > kmax)
			continue;
		const double x = (double)k - f;
		w[k - kmin] = (sin(cutoff * TM_SINC_PI * x) / (TM_SINC_PI * x)) * (0.5 + 0.5 * cos(x * (TM_SINC_PI / TM_SINC_HALF)));
	}
}

// ---------------------------------------------------------------------------
// dot products of n samples with n weights
// ---------------------------------------------------------------------------

#if defined(TM_SINC_NEON)

typedef float64x2_t tmSincV2;
typedef int32x4_t tmSincV4i;

static inline tmSincV2 tmSincZero(void) { return vdupq_n_f64(0.0); }
static inline double tmSincSum(const tmSincV2 x, const tmSincV2 y) { return vaddvq_f64(vaddq_f64(x, y)); }

static inline void tmSincAcc(const tmSincV4i v, const double *w, tmSincV2 *acc0, tmSincV2 *acc1)
{
	const float32x4_t f = vcvtq_f32_s32(v);
	*acc0 = vaddq_f64(*acc0, vmulq_f64(vcvt_f64_f32(vget_low_f32(f)), vld1q_f64(w)));
	*acc1 = vaddq_f64(*acc1, vmulq_f64(vcvt_f64_f32(vget_high_f32(f)), vld1q_f64(w + 2)));
}

static inline tmSincV4i tmSincLoad16(const int16_t *s) { return vmovl_s16(vld1_s16(s)); }

static inline tmSincV4i tmSincLoad8(const int8_t *s)
{
	uint32_t u;
	memcpy(&u, s, 4);
	return vmovl_s16(vget_low_s16(vmovl_s8(vcreate_s8((uint64_t)u))));
}

static inline void tmSincSplit(const int16x8_t x, tmSincV4i *l, tmSincV4i *r)
{
	const int16x4x2_t uz = vuzp_s16(vget_low_s16(x), vget_high_s16(x));
	*l = vmovl_s16(uz.val[0]);
	*r = vmovl_s16(uz.val[1]);
}

static inline void tmSincLoad16Stereo(const int16_t *s, tmSincV4i *l, tmSincV4i *r) { tmSincSplit(vld1q_s16(s), l, r); }
static inline void tmSincLoad8Stereo(const int8_t *s, tmSincV4i *l, tmSincV4i *r) { tmSincSplit(vmovl_s8(vld1_s8(s)), l, r); }

#elif defined(TM_SINC_SSE2)

typedef __m128d tmSincV2;
typedef __m128i tmSincV4i;

static inline tmSincV2 tmSincZero(void) { return _mm_setzero_pd(); }

static inline double tmSincSum(const tmSincV2 x, const tmSincV2 y)
{
	const __m128d t = _mm_add_pd(x, y);
	return _mm_cvtsd_f64(_mm_add_sd(t, _mm_unpackhi_pd(t, t)));
}

static inline void tmSincAcc(const tmSincV4i v, const double *w, tmSincV2 *acc0, tmSincV2 *acc1)
{
	*acc0 = _mm_add_pd(*acc0, _mm_mul_pd(_mm_cvtepi32_pd(v), _mm_loadu_pd(w)));
	*acc1 = _mm_add_pd(*acc1, _mm_mul_pd(_mm_cvtepi32_pd(_mm_unpackhi_epi64(v, v)), _mm_loadu_pd(w + 2)));
}

static inline tmSincV4i tmSincLoad16(const int16_t *s)
{
	const __m128i x = _mm_loadl_epi64((const __m128i *)s);
	return _mm_srai_epi32(_mm_unpacklo_epi16(x, x), 16);
}

static inline tmSincV4i tmSincLoad8(const int8_t *s)
{
	int32_t u;
	memcpy(&u, s, 4);
	__m128i x = _mm_cvtsi32_si128(u);
	x = _mm_srai_epi16(_mm_unpacklo_epi8(x, x), 8);
	return _mm_srai_epi32(_mm_unpacklo_epi16(x, x), 16);
}

static inline void tmSincSplit(const __m128i x, tmSincV4i *l, tmSincV4i *r)
{
	*l = _mm_srai_epi32(_mm_slli_epi32(x, 16), 16);
	*r = _mm_srai_epi32(x, 16);
}

static inline void tmSincLoad16Stereo(const int16_t *s, tmSincV4i *l, tmSincV4i *r) { tmSincSplit(_mm_loadu_si128((const __m128i *)s), l, r); }

static inline void tmSincLoad8Stereo(const int8_t *s, tmSincV4i *l, tmSincV4i *r)
{
	const __m128i x = _mm_loadl_epi64((const __m128i *)s);
	tmSincSplit(_mm_srai_epi16(_mm_unpacklo_epi8(x, x), 8), l, r);
}

#endif

#if defined(TM_SINC_NEON) || defined(TM_SINC_SSE2)
#define TM_SINC_DOT_SIMD(bits) \
	tmSincV2 acc0 = tmSincZero(), acc1 = tmSincZero(); \
	for (; j + 4 <= n; j += 4) \
		tmSincAcc(tmSincLoad##bits(s + j), w + j, &acc0, &acc1); \
	acc = tmSincSum(acc0, acc1);

#define TM_SINC_DOT_STEREO_SIMD(bits) \
	tmSincV2 l0 = tmSincZero(), l1 = tmSincZero(), r0 = tmSincZero(), r1 = tmSincZero(); \
	for (; j + 4 <= n; j += 4) \
	{ \
		tmSincV4i vl, vr; \
		tmSincLoad##bits##Stereo(s + j * 2, &vl, &vr); \
		tmSincAcc(vl, w + j, &l0, &l1); \
		tmSincAcc(vr, w + j, &r0, &r1); \
	} \
	accL = tmSincSum(l0, l1); \
	accR = tmSincSum(r0, r1);
#else
#define TM_SINC_DOT_SIMD(bits)
#define TM_SINC_DOT_STEREO_SIMD(bits)
#endif

#define TM_SINC_DEFINE_DOT(bits) \
	static inline double tmSincDot##bits(const int##bits##_t *s, const double *w, const int32_t n) \
	{ \
		double acc = 0.0; \
		int32_t j = 0; \
		TM_SINC_DOT_SIMD(bits) \
		for (; j < n; j++) \
			acc += (double)s[j] * w[j]; \
		return acc; \
	} \
	\
	/* s is interleaved, n counts frames */ \
	static inline void tmSincDot##bits##Stereo(const int##bits##_t *s, const double *w, const int32_t n, double *outL, double *outR) \
	{ \
		double accL = 0.0, accR = 0.0; \
		int32_t j = 0; \
		TM_SINC_DOT_STEREO_SIMD(bits) \
		for (; j < n; j++) \
		{ \
			accL += (double)s[j * 2] * w[j]; \
			accR += (double)s[j * 2 + 1] * w[j]; \
		} \
		*outL = accL; \
		*outR = accR; \
	}

TM_SINC_DEFINE_DOT(16)
TM_SINC_DEFINE_DOT(8)
