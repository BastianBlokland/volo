#pragma once
#include "geo_vector.h"

#include <immintrin.h>

/**
 * SIMD vector utilities using SSE, SSE2 and SSE3, SSE4 and SSE4.1 instructions.
 */

typedef __m128 SimdVec;

#define simd_vec_splat(_VEC_, _COMP_)                                                              \
  _mm_shuffle_ps((_VEC_), (_VEC_), _MM_SHUFFLE(_COMP_, _COMP_, _COMP_, _COMP_))

/**
 * Load 4 (128 bit aligned) float values into a Simd vector.
 * Pre-condition: bits_aligned_ptr(values, 16)
 */
INLINE_HINT static SimdVec simd_vec_load(const f32 values[4]) { return _mm_load_ps(values); }

/**
 * Store a Simd vector to 4 (128 bit aligned) float values.
 * Pre-condition: bits_aligned_ptr(values, 16)
 */
INLINE_HINT static void simd_vec_store(const SimdVec vec, f32 values[4]) {
  _mm_store_ps(values, vec);
}

INLINE_HINT static SimdVec simd_vec_zero() { return _mm_setzero_ps(); }

INLINE_HINT static f32 simd_vec_x(const SimdVec vec) { return _mm_cvtss_f32(vec); }

INLINE_HINT static SimdVec simd_vec_clear_w(const SimdVec vec) {
  static const u32 g_mask[4] = {~u32_lit(0), ~u32_lit(0), ~u32_lit(0), 0};
  // NOTE: Can we do this without a memory load?
  return _mm_and_ps(vec, simd_vec_load((f32*)g_mask));
}

INLINE_HINT static SimdVec simd_vec_set(const f32 a, const f32 b, const f32 c, const f32 d) {
  return _mm_set_ps(d, c, b, a);
}

INLINE_HINT static SimdVec simd_vec_broadcast(const f32 value) { return _mm_set1_ps(value); }

INLINE_HINT static SimdVec simd_vec_add(const SimdVec a, const SimdVec b) {
  return _mm_add_ps(a, b);
}

INLINE_HINT static SimdVec simd_vec_sub(const SimdVec a, const SimdVec b) {
  return _mm_sub_ps(a, b);
}

INLINE_HINT static SimdVec simd_vec_mul(const SimdVec a, const SimdVec b) {
  return _mm_mul_ps(a, b);
}

INLINE_HINT static SimdVec simd_vec_div(const SimdVec a, const SimdVec b) {
  return _mm_div_ps(a, b);
}

INLINE_HINT static SimdVec simd_vec_min(const SimdVec a, const SimdVec b) {
  return _mm_min_ps(a, b);
}

INLINE_HINT static SimdVec simd_vec_max(const SimdVec a, const SimdVec b) {
  return _mm_max_ps(a, b);
}

INLINE_HINT static SimdVec simd_vec_less(const SimdVec a, const SimdVec b) {
  return _mm_cmplt_ps(a, b);
}

INLINE_HINT static SimdVec simd_vec_greater(const SimdVec a, const SimdVec b) {
  return _mm_cmpgt_ps(a, b);
}

INLINE_HINT static bool simd_vec_any_true(const SimdVec a) { return _mm_movemask_ps(a) != 0x0000; }
INLINE_HINT static bool simd_vec_any_false(const SimdVec a) { return _mm_movemask_ps(a) != 0xffff; }
INLINE_HINT static bool simd_vec_all_true(const SimdVec a) { return _mm_movemask_ps(a) == 0xffff; }
INLINE_HINT static bool simd_vec_all_false(const SimdVec a) { return _mm_movemask_ps(a) == 0x0000; }

INLINE_HINT static SimdVec simd_vec_select(const SimdVec a, const SimdVec b, const SimdVec mask) {
  return _mm_blendv_ps(a, b, mask);
}

INLINE_HINT static SimdVec simd_vec_abs(const SimdVec a) {
  const SimdVec signBit = _mm_set1_ps(-0.0f);
  return _mm_andnot_ps(signBit, a);
}

INLINE_HINT static SimdVec simd_vec_dot4(const SimdVec a, const SimdVec b) {
  const SimdVec mul = _mm_mul_ps(a, b);
  const SimdVec t1  = _mm_hadd_ps(mul, mul);
  return _mm_hadd_ps(t1, t1);
}

INLINE_HINT static SimdVec simd_vec_dot3(const SimdVec a, const SimdVec b) {
  const SimdVec mul = _mm_mul_ps(a, b);
  const SimdVec t1  = simd_vec_clear_w(mul);
  const SimdVec t2  = _mm_hadd_ps(t1, t1);
  return _mm_hadd_ps(t2, t2);
}

INLINE_HINT static SimdVec simd_vec_sqrt(const SimdVec a) { return _mm_sqrt_ps(a); }

INLINE_HINT static SimdVec simd_vec_cross3(const SimdVec a, const SimdVec b) {
  const SimdVec t1  = _mm_shuffle_ps(a, a, _MM_SHUFFLE(3, 0, 2, 1));   // = (a.y, a.z, a.x, a.w)
  const SimdVec t2  = _mm_shuffle_ps(b, b, _MM_SHUFFLE(3, 1, 0, 2));   // = (b.z, b.x, b.y, b.w)
  SimdVec       res = _mm_mul_ps(t1, t2);                              // Perform the left operation
  const SimdVec t3  = _mm_shuffle_ps(t1, t1, _MM_SHUFFLE(3, 0, 2, 1)); // = (a.z, a.x, a.y, a.w)
  const SimdVec t4  = _mm_shuffle_ps(t2, t2, _MM_SHUFFLE(3, 1, 0, 2)); // = (b.y, b.z, b.x, b.w)
  res               = _mm_sub_ps(res, _mm_mul_ps(t3, t4)); // Perform the right operation
  return simd_vec_clear_w(res);
}

INLINE_HINT static SimdVec simd_quat_mul(const SimdVec xyzw, const SimdVec abcd) {
  /**
   * Multiply two quaternions.
   * Source: https://momchil-velikov.blogspot.com/2013/10/fast-sse-quternion-multiplication.html
   */
  const SimdVec wzyx = _mm_shuffle_ps(xyzw, xyzw, _MM_SHUFFLE(0, 1, 2, 3));
  const SimdVec baba = _mm_shuffle_ps(abcd, abcd, _MM_SHUFFLE(0, 1, 0, 1));
  const SimdVec dcdc = _mm_shuffle_ps(abcd, abcd, _MM_SHUFFLE(2, 3, 2, 3));

  /**
   * Naming: Variable names below indicate the parts of the result quat (X,Y,Z,W).
   * nX stands for -X and similarly for the other components.
   */

  // = (xb - ya, zb - wa, wd - zc, yd - xc)
  const SimdVec ZnXWY = _mm_hsub_ps(_mm_mul_ps(xyzw, baba), _mm_mul_ps(wzyx, dcdc));
  // = (xd + yc, zd + wc, wb + za, yb + xa)
  const SimdVec XZYnW = _mm_hadd_ps(_mm_mul_ps(xyzw, dcdc), _mm_mul_ps(wzyx, baba));
  // = (xd + yc, zd + wc, wd - zc, yd - xc)
  const SimdVec t1 = _mm_shuffle_ps(XZYnW, ZnXWY, _MM_SHUFFLE(3, 2, 1, 0));
  // = (zb - wa, xb - ya, yb + xa, wb + za)
  const SimdVec t2 = _mm_shuffle_ps(ZnXWY, XZYnW, _MM_SHUFFLE(2, 3, 0, 1));

  // = (xd + yc - zb + wa, xb - ya + zd + wc, wd - zc + yb + xa, yd - xc + wb + za)
  const SimdVec XZWY = _mm_addsub_ps(t1, t2);
  return _mm_shuffle_ps(XZWY, XZWY, _MM_SHUFFLE(2, 1, 3, 0));
}

INLINE_HINT static SimdVec simd_quat_rotate(const SimdVec quat, const SimdVec vec) {
  /**
   * NOTE: Very naive translation of our scalar implementation.
   */
  const SimdVec scalar     = simd_vec_splat(quat, 3);
  const SimdVec two        = simd_vec_broadcast(2.0f);
  const SimdVec scalar2    = simd_vec_mul(scalar, two);
  const SimdVec axis       = simd_vec_clear_w(quat);
  const SimdVec axisSqrMag = simd_vec_dot4(axis, axis);
  const SimdVec dot        = simd_vec_dot4(axis, vec);

  const SimdVec a = simd_vec_mul(axis, simd_vec_mul(dot, two));
  const SimdVec b = simd_vec_mul(vec, simd_vec_sub(simd_vec_mul(scalar, scalar), axisSqrMag));
  const SimdVec c = simd_vec_mul(simd_vec_cross3(axis, vec), scalar2);

  return simd_vec_add(simd_vec_add(a, b), c);
}

INLINE_HINT static SimdVec simd_quat_norm(const SimdVec quat) {
  const SimdVec sqrMag = simd_vec_dot4(quat, quat);
  const SimdVec mag    = simd_vec_sqrt(sqrMag);
  return simd_vec_div(quat, mag);
}
