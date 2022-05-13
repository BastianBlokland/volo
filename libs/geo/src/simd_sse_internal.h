#pragma once
#include "geo_vector.h"

#include <immintrin.h>

typedef __m128 SimdVec;

INLINE_HINT static SimdVec simd_vec_load(const f32 values[4]) { return _mm_load_ps(values); }

INLINE_HINT static void simd_vec_store(const SimdVec vec, f32 values[4]) {
  return _mm_store_ps(values, vec);
}

INLINE_HINT static SimdVec simd_vec_add(const SimdVec a, const SimdVec b) {
  return _mm_add_ps(a, b);
}

INLINE_HINT static SimdVec simd_vec_sub(const SimdVec a, const SimdVec b) {
  return _mm_sub_ps(a, b);
}
