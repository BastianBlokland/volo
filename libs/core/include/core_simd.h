#pragma once
#include "core_annotation.h"
#include "core_intrinsic.h"
#include "core_types.h"

#ifndef VOLO_SIMD
#error SIMD support not enabled
#endif

/**
 * SIMD vector utilities using SSE, SSE2 and SSE3, SSE4 and SSE4.1 instructions.
 * https://www.intel.com/content/www/us/en/docs/intrinsics-guide/index.html
 */

/**
 * Copy 128 bits from 'src' to 'dst'.
 */
MAYBE_UNUSED INLINE_HINT static void simd_copy_128(void* dst, const void* src) {
  _mm_storeu_si128(dst, _mm_loadu_si128(src));
}

typedef __m128 SimdVec;

#define simd_vec_shuffle(_A_, _B_, _C1_, _C2_, _C3_, _C4_)                                         \
  _mm_shuffle_ps((_A_), (_B_), _MM_SHUFFLE(_C1_, _C2_, _C3_, _C4_))

#define simd_vec_permute(_VEC_, _C1_, _C2_, _C3_, _C4_)                                            \
  simd_vec_shuffle((_VEC_), (_VEC_), _C1_, _C2_, _C3_, _C4_)

#define simd_vec_splat(_VEC_, _COMP_)                                                              \
  simd_vec_permute((_VEC_), (_COMP_), (_COMP_), (_COMP_), (_COMP_))

#define simd_vec_shift_left(_VEC_, _AMOUNT_)                                                       \
  _mm_castsi128_ps(_mm_slli_epi32(_mm_castps_si128(_VEC_), _AMOUNT_))

#define simd_vec_shift_right(_VEC_, _AMOUNT_)                                                      \
  _mm_castsi128_ps(_mm_srli_epi32(_mm_castps_si128(_VEC_), _AMOUNT_))

#define simd_vec_shift_right_sign(_VEC_, _AMOUNT_)                                                 \
  _mm_castsi128_ps(_mm_srai_epi32(_mm_castps_si128(_VEC_), _AMOUNT_))

/**
 * Load 4 (128 bit aligned) float values into a Simd vector.
 * Pre-condition: bits_aligned_ptr(values, 16)
 */
MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_load(const f32 values[PARAM_ARRAY_SIZE(4)]) {
  return _mm_load_ps(values);
}

/**
 * Load 16 (128 bit aligned) u8 values into a Simd vector.
 * Pre-condition: bits_aligned_ptr(values, 16)
 */
MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_load_u8(const u8 values[PARAM_ARRAY_SIZE(16)]) {
  return _mm_load_ps((const f32*)values);
}

/**
 * Load 8 (128 bit aligned) u16 values into a Simd vector.
 * Pre-condition: bits_aligned_ptr(values, 16)
 */
MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_load_u16(const u16 values[PARAM_ARRAY_SIZE(8)]) {
  return _mm_load_ps((const f32*)values);
}

/**
 * Load 4 (128 bit aligned) u32 values into a Simd vector.
 * Pre-condition: bits_aligned_ptr(values, 16)
 */
MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_load_u32(const u32 values[PARAM_ARRAY_SIZE(4)]) {
  return _mm_load_ps((const f32*)values);
}

/**
 * Store a Simd vector to 4 (128 bit aligned) float values.
 * Pre-condition: bits_aligned_ptr(values, 16)
 */
MAYBE_UNUSED INLINE_HINT static void
simd_vec_store(const SimdVec vec, f32 values[PARAM_ARRAY_SIZE(4)]) {
  _mm_store_ps(values, vec);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_zero(void) { return _mm_setzero_ps(); }

MAYBE_UNUSED INLINE_HINT static f32 simd_vec_x(const SimdVec vec) { return _mm_cvtss_f32(vec); }

MAYBE_UNUSED INLINE_HINT static u64 simd_vec_u64(const SimdVec vec) {
  return _mm_cvtsi128_si64(_mm_castps_si128(vec));
}

MAYBE_UNUSED INLINE_HINT static SimdVec
simd_vec_set(const f32 a, const f32 b, const f32 c, const f32 d) {
  return _mm_set_ps(d, c, b, a);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_broadcast(const f32 value) {
  return _mm_set1_ps(value);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_broadcast_u16(const u16 value) {
  return _mm_castsi128_ps(_mm_set1_epi16(value));
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_broadcast_u32(const u32 value) {
  return _mm_castsi128_ps(_mm_set1_epi32(value));
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_sign_mask(void) {
  return simd_vec_broadcast(-0.0f);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_sign_mask3(void) {
  return simd_vec_set(-0.0f, -0.0f, -0.0f, 0.0f);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_clear_w(const SimdVec vec) {
  // Use a 4 byte shift to clear the w component.
  return _mm_castsi128_ps(_mm_srli_si128(_mm_slli_si128(_mm_castps_si128(vec), 4), 4));
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_copy_w(const SimdVec dst, const SimdVec src) {
  return _mm_blend_ps(dst, src, 0b1000);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_w_one(const SimdVec vec) {
  return simd_vec_copy_w(vec, simd_vec_broadcast(1.0f));
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_add(const SimdVec a, const SimdVec b) {
  return _mm_add_ps(a, b);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_sub(const SimdVec a, const SimdVec b) {
  return _mm_sub_ps(a, b);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_sub_i32(const SimdVec a, const SimdVec b) {
  return _mm_castsi128_ps(_mm_sub_epi32(_mm_castps_si128(a), _mm_castps_si128(b)));
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_mul(const SimdVec a, const SimdVec b) {
  return _mm_mul_ps(a, b);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_div(const SimdVec a, const SimdVec b) {
  return _mm_div_ps(a, b);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_xor(const SimdVec a, const SimdVec b) {
  return _mm_xor_ps(a, b);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_min(const SimdVec a, const SimdVec b) {
  return _mm_min_ps(a, b);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_max(const SimdVec a, const SimdVec b) {
  return _mm_max_ps(a, b);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_less(const SimdVec a, const SimdVec b) {
  return _mm_cmplt_ps(a, b);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_greater(const SimdVec a, const SimdVec b) {
  return _mm_cmpgt_ps(a, b);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_eq_u32(const SimdVec a, const SimdVec b) {
  return _mm_castsi128_ps(_mm_cmpeq_epi32(_mm_castps_si128(a), _mm_castps_si128(b)));
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_and(const SimdVec a, const SimdVec b) {
  return _mm_and_ps(a, b);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_or(const SimdVec a, const SimdVec b) {
  return _mm_or_ps(a, b);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_pack_u32_to_u16(const SimdVec a, const SimdVec b) {
  return _mm_castsi128_ps(_mm_packs_epi32(_mm_castps_si128(a), _mm_castps_si128(b)));
}

MAYBE_UNUSED INLINE_HINT static u32 simd_vec_mask_u32(const SimdVec a) {
  return _mm_movemask_ps(a);
}

MAYBE_UNUSED INLINE_HINT static u32 simd_vec_mask_u8(const SimdVec a) {
  return _mm_movemask_epi8(_mm_castps_si128(a));
}

MAYBE_UNUSED INLINE_HINT static SimdVec
simd_vec_select(const SimdVec a, const SimdVec b, const SimdVec mask) {
  return _mm_blendv_ps(a, b, mask);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_reciprocal(const SimdVec vec) {
  return _mm_rcp_ps(vec);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_min_comp(SimdVec vec) {
  SimdVec tmp = simd_vec_permute(vec, 2, 3, 2, 3);
  tmp         = simd_vec_min(vec, tmp);
  vec         = simd_vec_permute(tmp, 1, 1, 1, 1);
  return simd_vec_min(vec, tmp);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_min_comp3(SimdVec vec) {
  SimdVec tmp = simd_vec_permute(vec, 2, 2, 2, 2);
  tmp         = simd_vec_min(vec, tmp);
  vec         = simd_vec_permute(tmp, 1, 1, 1, 1);
  return simd_vec_min(vec, tmp);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_max_comp(SimdVec vec) {
  SimdVec tmp = simd_vec_permute(vec, 2, 3, 2, 3);
  tmp         = simd_vec_max(vec, tmp);
  vec         = simd_vec_permute(tmp, 1, 1, 1, 1);
  return simd_vec_max(vec, tmp);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_max_comp3(SimdVec vec) {
  SimdVec tmp = simd_vec_permute(vec, 2, 2, 2, 2);
  tmp         = simd_vec_max(vec, tmp);
  vec         = simd_vec_permute(tmp, 1, 1, 1, 1);
  return simd_vec_max(vec, tmp);
}

/**
 * Convert four 32 bit floating point values to 16 bit.
 * NOTE: Requires the F16C extension.
 */
MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_f32_to_f16(const SimdVec vec) {
  return _mm_castsi128_ps(_mm_cvtps_ph(vec, _MM_FROUND_TO_NEAREST_INT));
}

/**
 * Convert four 32 bit floating point values to 16 bit.
 * This is much simpler (and faster) then 'float_f32_to_f16()' but has limitations:
 * - NaN is not supported.
 * - Inf and -Inf are not supported.
 * - Values that overflow f16 are undefined.
 * - Values that underflow f16 are not guaranteed to return zero.
 * - Denormals are not supported.
 *
 * It does make the following guarantees however:
 * - Integers 0 - 1023 (inclusive) are represented exactly.
 */
MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_f32_to_f16_soft(const SimdVec vec) {
  const SimdVec maskFF  = simd_vec_broadcast_u32(0xFF);
  const SimdVec mask3FF = simd_vec_broadcast_u32(0x3FF);
  const SimdVec mask70  = simd_vec_broadcast_u32(0x70);

  /**
   * Implementation adapted from 'sam hocevar's answer on StackOverflow:
   * - https://stackoverflow.com/questions/3026441/float32-to-float16
   */
  const SimdVec a   = simd_vec_shift_left(simd_vec_shift_right(vec, 31), 5);
  const SimdVec b   = simd_vec_and(simd_vec_shift_right(vec, 13), mask3FF);
  const SimdVec c   = simd_vec_and(simd_vec_shift_right(vec, 23), maskFF);
  const SimdVec d   = simd_vec_sub_i32(mask70, c);
  const SimdVec e   = simd_vec_shift_right(simd_vec_shift_right_sign(d, 4), 27);
  const SimdVec f   = simd_vec_and(simd_vec_sub_i32(c, mask70), e);
  SimdVec       res = simd_vec_or(simd_vec_shift_left(simd_vec_or(a, f), 10), b);

  /**
   * The four 16 bit floats have now been computed, move them to the bottom 64 bits of the vector.
   * [x, 0, y, 0, z, 0, w, 0] -> [x, y, z, w, 0, 0, 0, 0]
   */
  res = _mm_castsi128_ps(_mm_shufflehi_epi16(_mm_castps_si128(res), _MM_SHUFFLE(0, 0, 2, 0)));
  res = _mm_castsi128_ps(_mm_shufflelo_epi16(_mm_castps_si128(res), _MM_SHUFFLE(0, 0, 2, 0)));
  return simd_vec_permute(res, 0, 0, 2, 0);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_abs(const SimdVec vec) {
  return _mm_andnot_ps(simd_vec_sign_mask(), vec);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_sign(const SimdVec vec) {
  return _mm_and_ps(vec, simd_vec_sign_mask());
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_neg(const SimdVec vec) {
  return _mm_xor_ps(vec, simd_vec_sign_mask());
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_neg3(const SimdVec vec) {
  return _mm_xor_ps(vec, simd_vec_sign_mask3());
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_round_nearest(const SimdVec a) {
  return _mm_round_ps(a, _MM_FROUND_TO_NEAREST_INT | _MM_FROUND_NO_EXC);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_round_down(const SimdVec a) {
  return _mm_round_ps(a, _MM_FROUND_TO_NEG_INF | _MM_FROUND_NO_EXC);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_round_up(const SimdVec a) {
  return _mm_round_ps(a, _MM_FROUND_TO_POS_INF | _MM_FROUND_NO_EXC);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_dot4(const SimdVec a, const SimdVec b) {
  return _mm_dp_ps(a, b, 0b11111111);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_dot3(const SimdVec a, const SimdVec b) {
  return _mm_dp_ps(a, b, 0b01111111);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_sqrt(const SimdVec a) { return _mm_sqrt_ps(a); }

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_rsqrt(const SimdVec v) {
  /**
   * Compute the reciprocal square root (1.0 / simd_vec_sqrt(v)).
   * Use a single Newton-Raphson step to increase accuracy from 12 to 23 bits.
   * Source:
   *  https://stackoverflow.com/questions/14752399/newton-raphson-with-sse2-can-someone-explain-me-these-3-lines
   */
  const SimdVec half  = simd_vec_broadcast(0.5f);
  const SimdVec three = simd_vec_broadcast(3.0f);
  const SimdVec rcp   = _mm_rsqrt_ps(v);
  const SimdVec mul   = simd_vec_mul(simd_vec_mul(v, rcp), rcp);
  return simd_vec_mul(simd_vec_mul(half, rcp), simd_vec_sub(three, mul));
}

MAYBE_UNUSED INLINE_HINT static void simd_vec_sincos(const SimdVec v, SimdVec* sin, SimdVec* cos) {
  // TODO: Implement a sse sincos, something along the lines of http://gruntthepeon.free.fr/ssemath/
#if defined(VOLO_MSVC)
  *sin = _mm_sincos_ps(cos, v);
#else
  *sin = simd_vec_broadcast(intrinsic_sin_f32(simd_vec_x(v)));
  *cos = simd_vec_broadcast(intrinsic_cos_f32(simd_vec_x(v)));
#endif
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_vec_cross3(const SimdVec a, const SimdVec b) {
  const SimdVec t1  = simd_vec_permute(a, 3, 0, 2, 1);  // = (a.y, a.z, a.x, a.w)
  const SimdVec t2  = simd_vec_permute(b, 3, 1, 0, 2);  // = (b.z, b.x, b.y, b.w)
  SimdVec       res = simd_vec_mul(t1, t2);             // Perform the left operation
  const SimdVec t3  = simd_vec_permute(t1, 3, 0, 2, 1); // = (a.z, a.x, a.y, a.w)
  const SimdVec t4  = simd_vec_permute(t2, 3, 1, 0, 2); // = (b.y, b.z, b.x, b.w)
  return simd_vec_sub(res, simd_vec_mul(t3, t4));       // Perform the right operation
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_quat_mul(const SimdVec xyzw, const SimdVec abcd) {
  /**
   * Multiply two quaternions.
   * Source: https://momchil-velikov.blogspot.com/2013/10/fast-sse-quternion-multiplication.html
   */
  const SimdVec wzyx = simd_vec_permute(xyzw, 0, 1, 2, 3);
  const SimdVec baba = simd_vec_permute(abcd, 0, 1, 0, 1);
  const SimdVec dcdc = simd_vec_permute(abcd, 2, 3, 2, 3);

  /**
   * Naming: Variable names below indicate the parts of the result quat (X,Y,Z,W).
   * nX stands for -X and similarly for the other components.
   */

  // = (xb - ya, zb - wa, wd - zc, yd - xc)
  const SimdVec ZnXWY = _mm_hsub_ps(simd_vec_mul(xyzw, baba), simd_vec_mul(wzyx, dcdc));
  // = (xd + yc, zd + wc, wb + za, yb + xa)
  const SimdVec XZYnW = _mm_hadd_ps(simd_vec_mul(xyzw, dcdc), simd_vec_mul(wzyx, baba));
  // = (xd + yc, zd + wc, wd - zc, yd - xc)
  const SimdVec t1 = simd_vec_shuffle(XZYnW, ZnXWY, 3, 2, 1, 0);
  // = (zb - wa, xb - ya, yb + xa, wb + za)
  const SimdVec t2 = simd_vec_shuffle(ZnXWY, XZYnW, 2, 3, 0, 1);

  // = (xd + yc - zb + wa, xb - ya + zd + wc, wd - zc + yb + xa, yd - xc + wb + za)
  const SimdVec XZWY = _mm_addsub_ps(t1, t2);
  return simd_vec_permute(XZWY, 2, 1, 3, 0);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_quat_rotate(const SimdVec quat, const SimdVec vec) {
  const SimdVec scalar = simd_vec_splat(quat, 3);
  const SimdVec axis   = simd_vec_clear_w(quat);
  const SimdVec a      = simd_vec_cross3(axis, vec);
  const SimdVec b      = simd_vec_cross3(axis, simd_vec_add(a, simd_vec_mul(vec, scalar)));
  return simd_vec_add(vec, simd_vec_mul(b, simd_vec_broadcast(2.0f)));
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_quat_conjugate(const SimdVec quat) {
  return simd_vec_neg3(quat);
}

MAYBE_UNUSED INLINE_HINT static SimdVec simd_quat_norm(const SimdVec quat) {
  const SimdVec sqrMag = simd_vec_dot4(quat, quat);
  return simd_vec_mul(quat, simd_vec_rsqrt(sqrMag));
}
