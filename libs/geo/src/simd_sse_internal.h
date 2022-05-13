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

INLINE_HINT static SimdVec simd_vec_dot4(const SimdVec a, SimdVec b) {
  SimdVec tmp = _mm_mul_ps(a, b);
  b           = _mm_shuffle_ps(b, tmp, _MM_SHUFFLE(1, 0, 0, 0)); // w = a.y * b.y
  b           = _mm_add_ps(b, tmp);
  tmp         = _mm_shuffle_ps(tmp, b, _MM_SHUFFLE(0, 3, 0, 0)); // z = (a.y * b.y) + (a.w * b.w)
  tmp         = _mm_add_ps(tmp, b); // z = (a.y * b.y) + (a.w * b.w) + (a.x * b.x) + (a.z * b.z)
  return _mm_shuffle_ps(tmp, tmp, _MM_SHUFFLE(2, 2, 2, 2)); // Splat the result.
}
