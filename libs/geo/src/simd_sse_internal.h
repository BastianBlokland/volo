#pragma once
#include "geo_vector.h"

#include <immintrin.h>

typedef __m128 SimdVec;

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

INLINE_HINT static f32 simd_vec_x(const SimdVec vec) { return _mm_cvtss_f32(vec); }

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

INLINE_HINT static SimdVec simd_vec_dot4(const SimdVec a, const SimdVec b) {
  const SimdVec mul = _mm_mul_ps(a, b);
  const SimdVec t1  = _mm_shuffle_ps(b, mul, _MM_SHUFFLE(1, 0, 0, 0)); // w = a.y * b.y
  const SimdVec t2  = _mm_add_ps(t1, mul);
  const SimdVec t3 = _mm_shuffle_ps(mul, t2, _MM_SHUFFLE(0, 3, 0, 0)); // z = (a.y *b.y) + (a.w*b.w)
  const SimdVec t4 = _mm_add_ps(t3, t2); // z = (a.y * b.y) + (a.w * b.w) + (a.x * b.x) + (a.z *b.z)
  return _mm_shuffle_ps(t4, t4, _MM_SHUFFLE(2, 2, 2, 2)); // Splat the result.
}
