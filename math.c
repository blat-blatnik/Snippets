// Polynomial approximations to trigonometric and exponential functions.
// Trigometric functions work in turns instead of radians, 1 turn = 2pi.

// === compiler intrinsics ===

#ifdef _MSC_VER
#include <intrin.h>
#endif

#if !defined _MSC_VER || defined __cplusplus
#define B_NAN __builtin_nanf("")
#else
#define B_NAN (-(float)(1e300 * 1e300 * 0))
#endif

static inline float b_abs(float x) {
#ifdef _MSC_VER
	return _mm_cvtss_f32(_mm_andnot_ps(_mm_set_ss(-0.0f), _mm_set_ss(x)));
#else
	return __builtin_fabsf(x);
#endif
}
static inline float b_floor(float x) {
#ifdef _MSC_VER
	return __floorf(x);
#else
	return __builtin_floorf(x);
#endif
}
static inline float b_round(float x) {
#ifdef _MSC_VER
	return __roundf(x);
#else
	return __builtin_roundf(x);
#endif
}
static inline float b_sqrt(float x) {
#ifdef _MSC_VER
	return __sqrt_ss(x);
#else
	return __builtin_sqrtf(x);
#endif
}
static inline float b_copysign(float dst, float src) {
#ifdef _MSC_VER
	return __copysignf(dst, src);
#else
	return __builtin_copysignf(dst, src);
#endif
}
static inline float float_from_bits(unsigned x) {
#if _MSC_VER
	return _castu32_f32(x);
#else
	union { unsigned u; float f; } fu = { x };
	return fu.f;
#endif
}
static inline unsigned bits_from_float(float x) {
#if _MSC_VER
	return _castf32_u32(x);
#else
	union { float f; unsigned u; } fu = { x };
	return fu.u;
#endif
}

// === base functions ===

static void b_sincos(float turns, float out[2]) {
	// https://marc-b-reynolds.github.io/math/2020/03/11/SinCosPi.html
	// range reduction to [0,1/8]: sin/cos(x) = sin/cos(x + 2pi), sin/cos(x) = -sin/cos(x + pi) sin(x) = cos(x + pi/2)
	float range = b_round(4 * turns);
	unsigned quadrant = (unsigned)(long long)range;
	float x = turns - 0.25f * range;
	float x2 = x * x;

	// set up range reconstruction
	unsigned sign_x = (quadrant >> 1) << 31;
	unsigned sign_y = (quadrant << 31) ^ sign_x;
	quadrant &= 1;

	// sollya> fpminimax(sin(2*pi*x), [|1,3,5,7|], [|24...|], [|0;1/8|], floating, relative);
	// max error = 5.382e-9
	float s = -75.83747100830078125f;
	s = s * x2 + 81.6046142578125f;
	s = s * x2 - 41.34175872802734375f;
	s = s * x2 + 6.283185482025146484375f;
	s = s * x;

	// sollya> fpminimax(cos(2*pi*x), [|0,2,4,6|], [|24...|], [|0;1/8|], floating, relative);
	// max error = 5.960e-8
	float c = -83.49729156494140625f;
	c = c * x2 + 64.9187469482421875f;
	c = c * x2 - 19.7391338348388671875f;
	c = c * x2 + 0.999999940395355224609375f;

	// reconstruct full range
	s = float_from_bits(bits_from_float(s) ^ sign_y);
	c = float_from_bits(bits_from_float(c) ^ sign_x);

	// store in the correct component
	out[quadrant ^ 0] = s;
	out[quadrant ^ 1] = c;
}
static float b_atan2(float y, float x) {
	// https://mazzo.li/posts/vectorized-atan2.html modified to correctly handle -0
	// range reduce to [0,1]: atan(x) = pi/2 - atan(1/x)
	int swap = b_abs(x) < b_abs(y);
	float num = swap ? x : y;
	float den = swap ? y : x;
	float yoverx = num / den;

	// range reduce to [0,1/4]: atan(x) = b + atan((x - k) / (1 + kx))
	// https://basesandframes.files.wordpress.com/2016/05/fast-math-functions_p2.pdf#page=35
	float abs = b_abs(yoverx);
	float k = (abs < 0.5f) ? 0.25f : 0.75f;
	float b = (abs < 0.5f) ? 0.03898956518868466f : 0.10241638234956672f;
	float input = (abs - k) / (1 + k * abs);

	// rvaluate atan(x) polynomial in [0,1/4]
	// sollya> fpminimax(atan(x)/(2*pi), [|1,3,5,7|], [|24...|], [|1e-50;1/4|], floating, relative);
	// max error = 2.998e-10
	float input2 = input * input;
	float angle = -2.05062441527843475341796875e-2f;
	angle = angle * input2 + 3.17338518798351287841796875e-2f;
	angle = angle * input2 - 5.30500970780849456787109375e-2f;
	angle = angle * input2 + 0.15915493667125701904296875f;
	angle = angle * input;

	// reconstruct full range
	angle = b_copysign(b + angle, yoverx);
	float unswap_angle = b_copysign(0.25f, yoverx) - angle;
	angle = swap ? unswap_angle : angle;
	float quadrant_correction = b_copysign(0.5f, y);
	angle += bits_from_float(x) & 0x80000000 ? quadrant_correction : 0;
	return angle;
}
static float b_exp2(float x) {
	// range reduce: 2^x = 2^i * 2^f, where i is the integer part of x and f is the fraction
	float i = b_floor(x);
	float f = x - i;

	// compute 2^f via polynomial approximation
	// sollya> fpminimax(2^x, [|0,1,2,3,4,5,6|], [|24...|], [|0;1|], floating, relative);
	// max error = 4.293e-9
	float exp2f = 2.15564403333701193332672119140625e-4f;
	exp2f = exp2f * f + 1.248489017598330974578857421875e-3f;
	exp2f = exp2f * f + 9.67352092266082763671875e-3f;
	exp2f = exp2f * f + 5.54862879216670989990234375e-2f;
	exp2f = exp2f * f + 0.240229070186614990234375f;
	exp2f = exp2f * f + 0.69314706325531005859375f;
	exp2f = exp2f * f + 1.0f;

	// compute 2^i by directly loading i into the exponent
	int exponent = (int)i + 127;
	exponent = exponent < 0 ? 0 : exponent;
	exponent = exponent > 255 ? 255 : exponent;
	float exp2i = float_from_bits(exponent << 23);

	return exp2f * exp2i;
}
static float b_log2(float x) {
	// range reduce: log2(x) = log2(2^e * 1.f) = e + log2(1.f)
	unsigned bits = bits_from_float(x);
	float e = (float)(bits >> 23) - 127;
	float f = float_from_bits((bits & 0x007FFFFF) | 0x3F800000); // set exponent to 0

	// sollya> fpminimax(1+log2(x+1), [|0,1,2,3,4,5,6|], [|24...|], [0;1]);
	// max error = 2.587e-6
	f -= 1;
	float log2f = -2.701638080179691314697265625e-2f;
	log2f = log2f * f + 0.12492744624614715576171875f;
	log2f = log2f * f - 0.2808862030506134033203125f;
	log2f = log2f * f + 0.4587285518646240234375f;
	log2f = log2f * f - 0.71829402446746826171875f;
	log2f = log2f * f + 1.44253671169281005859375f;
	log2f = log2f * f + 0.00000131130218505859375f; // -1 because this is 1+log2

	// reconstruct full range
	float log2 = e + log2f;
	return e > 127 ? B_NAN : log2; // log(negative) = NaN
}

// === functions derived from base functions ====

static inline float b_sin(float turns) {
	float sc[2];
	b_sincos(turns, sc);
	return sc[0];
}
static inline float b_cos(float turns) {
	float sc[2];
	b_sincos(turns, sc);
	return sc[1];
}
static inline float b_tan(float turns) {
	float sc[2];
	b_sincos(turns, sc);
	return sc[0] / sc[1];
}
static inline float b_asin(float y) {
	return b_atan2(y, b_sqrt(1 - y * y));
}
static inline float b_acos(float x) {
	return b_atan2(b_sqrt(1 - x * x), x);
}
static inline float b_atan(float yoverx) {
	return b_atan2(yoverx, 1);
}
static inline float b_sinh(float turns) {
	float expx = b_exp2(turns * 9.064720283654387f); // log2(e)*tau
	return (expx - 1 / expx) * 0.5f;
}
static inline float b_cosh(float turns) {
	float expx = b_exp2(turns * 9.064720283654387f); // log2(e)*tau
	return (expx + 1 / expx) * 0.5f;
}
static inline float b_tanh(float turns) {
	float exp2x = b_exp2(turns * 18.129440567308773f); // 2*log2(e)*tau
	return (exp2x - 1) / (exp2x + 1);
}
static inline float b_asinh(float y) {
	return b_log2(y + b_sqrt(y * y + 1)) / 9.064720283654387f; // log2(e)*tau
}
static inline float b_acosh(float x) {
	return b_log2(x + b_sqrt(x * x - 1)) / 9.064720283654387f; // log2(e)*tau
}
static inline float b_atanh(float yoverx) {
	return b_log2((1 + yoverx) / (1 - yoverx)) / 18.129440567308773f; // 2*log2(e)*tau
}
static inline float b_exp(float x) {
	return b_exp2(x * 1.4426950408889634f); // log2(e)
}
static inline float b_log(float x) {
	return b_log2(x) / 1.4426950408889634f; // log2(e)
}
static inline float b_log10(float x) {
	return b_log2(x) / 3.3219280948873626f; // log2(10)
}
static inline float b_pow(float x, float y) {
	return b_exp2(y * b_log2(x));
}

// === tests ===

#include <assert.h>

int main() {
	// atan2(sin(x),cos(x)) == x
	for (int deg = -1000 * 360; deg <= +1000 * 360; deg++) {
		float turns = (float)deg / 360.0f;
		float y = b_sin(turns);
		float x = b_cos(turns);
		float t = b_atan2(y, x);
		float a = turns - b_floor(turns); // wrap to [0,1)
		float b = t - b_floor(t); // wrap to [0,1)
		float d = b_abs(a - b);
		assert(d < 1e-7f);
	}

	// exp2(int) is exact and log2(int) is within 1e-5
	for (int pow2 = -30; pow2 <= +30; pow2++) {
		float a = pow2 >= 0 ? (float)(1 << pow2) : 1 / (float)(1 << -pow2);
		float b = b_exp2((float)pow2);
		float c = b_log2(a);
		float d = b_abs(c - (float)pow2);
		assert(a == b);
		assert(d < 1e-5f);
	}

	// pow(x,0.5) is close to sqrt
	for (int i = 1; i <= +9999; i++) {
		float f = (float)i;
		float a = b_sqrt(f);
		float b = b_pow(f, 0.5f);
		float d = b_abs(a - b) / a;
		assert(d < 1e-5f);
	}

	// pow(x,3) is close to x*x*x
	for (int i = 1; i <= +9999; i++) {
		float f = (float)i;
		float a = f * f * f;
		float b = b_pow(f, 3);
		float d = b_abs(a - b) / a;
		assert(d < 1e-5f);
	}
}
