// Web port (paths agent, match rule locoSimd): the locomotor's heading atan2 as a fixed polynomial
// (Cephes atanf range reduction), only + - * / and compares, so every browser gets the same floats and
// the 4-lane wasm SIMD form (gxLocoAtan2x4) gives exactly the scalar result in every lane.
// Accuracy: about 1e-7 rad against libm. Not libm's result, hence the match rule.
#pragma once
#ifdef __EMSCRIPTEN__
#include <wasm_simd128.h>
#endif

static inline float gxLocoAtan2(float y, float x)
{
	const float ax = __builtin_fabsf(x), ay = __builtin_fabsf(y);	// as wasm_f32x4_abs (-0 -> +0)
	const bool swap = ay > ax;
	const float num = swap ? ax : ay, den = swap ? ay : ax;
	float t = den > 0.0f ? num / den : 0.0f;	// [0, 1]
	const bool red = t > 0.414213562373f;		// tan(pi/8)
	const float base = red ? 0.785398163397f : 0.0f;
	t = red ? (t - 1.0f) / (t + 1.0f) : t;
	const float z = t * t;
	float r = ((((8.05374449538e-2f * z - 1.38776856032e-1f) * z + 1.99777106478e-1f) * z - 3.33329491539e-1f) * z) * t + t;
	r = base + r;
	r = swap ? 1.57079632679f - r : r;
	r = x < 0.0f ? 3.14159265359f - r : r;
	return y < 0.0f ? -r : r;
}

#ifdef __EMSCRIPTEN__
// 4 lanes, the same operations in the same order as gxLocoAtan2 (selects instead of branches)
static inline v128_t gxLocoAtan2x4(v128_t y, v128_t x)
{
	const v128_t zero = wasm_f32x4_splat(0.0f);
	const v128_t ax = wasm_f32x4_abs(x), ay = wasm_f32x4_abs(y);
	const v128_t swap = wasm_f32x4_gt(ay, ax);
	const v128_t num = wasm_v128_bitselect(ax, ay, swap), den = wasm_v128_bitselect(ay, ax, swap);
	const v128_t dpos = wasm_f32x4_gt(den, zero);
	v128_t t = wasm_v128_bitselect(wasm_f32x4_div(num, wasm_v128_bitselect(den, wasm_f32x4_splat(1.0f), dpos)), zero, dpos);
	const v128_t one = wasm_f32x4_splat(1.0f);
	const v128_t red = wasm_f32x4_gt(t, wasm_f32x4_splat(0.414213562373f));
	const v128_t base = wasm_v128_and(wasm_f32x4_splat(0.785398163397f), red);
	t = wasm_v128_bitselect(wasm_f32x4_div(wasm_f32x4_sub(t, one), wasm_f32x4_add(t, one)), t, red);
	const v128_t z = wasm_f32x4_mul(t, t);
	v128_t r = wasm_f32x4_sub(wasm_f32x4_mul(wasm_f32x4_splat(8.05374449538e-2f), z), wasm_f32x4_splat(1.38776856032e-1f));
	r = wasm_f32x4_add(wasm_f32x4_mul(r, z), wasm_f32x4_splat(1.99777106478e-1f));
	r = wasm_f32x4_sub(wasm_f32x4_mul(r, z), wasm_f32x4_splat(3.33329491539e-1f));
	r = wasm_f32x4_add(wasm_f32x4_mul(wasm_f32x4_mul(r, z), t), t);
	r = wasm_f32x4_add(base, r);
	r = wasm_v128_bitselect(wasm_f32x4_sub(wasm_f32x4_splat(1.57079632679f), r), r, swap);
	r = wasm_v128_bitselect(wasm_f32x4_sub(wasm_f32x4_splat(3.14159265359f), r), r, wasm_f32x4_lt(x, zero));
	return wasm_v128_bitselect(wasm_f32x4_neg(r), r, wasm_f32x4_lt(y, zero));
}
#endif
