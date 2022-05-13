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

INLINE_HINT static SimdVec simd_vec_add(const SimdVec a, const SimdVec b) {
  return _mm_add_ps(a, b);
}

INLINE_HINT static SimdVec simd_vec_sub(const SimdVec a, const SimdVec b) {
  return _mm_sub_ps(a, b);
}
