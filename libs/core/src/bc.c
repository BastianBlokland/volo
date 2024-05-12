#include "core_array.h"
#include "core_bc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_intrinsic.h"
#include "core_math.h"

/**
 * Texture Block Compression.
 *
 * Specification:
 * https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.html#S3TC
 * https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#BCFormats
 *
 * References:
 * https://sjbrown.co.uk/posts/dxt-compression-techniques/
 * https://fgiesen.wordpress.com/2022/11/08/whats-that-magic-computation-in-stb__refineblock/
 * 'Real-Time DXT Compression by J.M.P. van Waveren, 2006, Id Software, Inc.':
 *     https://www.researchgate.net/publication/259000525_Real-Time_DXT_Compression
 * https://fgiesen.wordpress.com/2021/10/04/gpu-bcn-decoding/
 *
 * NOTE: This encoder assumes a little-endian host system.
 */

typedef struct {
  f32 x, y, z;
} BcVec;

INLINE_HINT static BcVec bc_vec_mul(const BcVec a, const f32 scalar) {
  return (BcVec){a.x * scalar, a.y * scalar, a.z * scalar};
}

INLINE_HINT static f32 bc_vec_max(const BcVec a) { return math_max(a.x, math_max(a.y, a.z)); }

INLINE_HINT static u32 bc_color_dist3_sqr(const BcColor8888 a, const BcColor8888 b) {
  const i32 dR = b.r - a.r;
  const i32 dG = b.g - a.g;
  const i32 dB = b.b - a.b;
  return dR * dR + dG * dG + dB * dB;
}

INLINE_HINT static f32 bc_color_dot3(const BcColor8888 c, const BcVec axis) {
  return (f32)c.r * axis.x + (f32)c.g * axis.y + (f32)c.b * axis.z;
}

/**
 * Convert a 888 color to 565 with proper rounding.
 * Constants have been derived by 'Anonymous':
 * https://stackoverflow.com/questions/2442576/how-does-one-convert-16-bit-rgb565-to-24-bit-rgb888
 */
INLINE_HINT static BcColor565 bc_color_to_565(const BcColor8888 c) {
  const u8 r = (c.r * 249 + 1014) >> 11;
  const u8 g = (c.g * 253 + 505) >> 10;
  const u8 b = (c.b * 249 + 1014) >> 11;
  return (BcColor565){r << 11 | g << 5 | b};
}

/**
 * Convert a 565 color to 888 with proper rounding.
 * Constants have been derived by 'Anonymous':
 * https://stackoverflow.com/questions/2442576/how-does-one-convert-16-bit-rgb565-to-24-bit-rgb888
 */
INLINE_HINT static BcColor8888 bc_color_from_565(const BcColor565 c) {
  const u8 r = ((c >> 11 & 0x1F) * 527 + 23) >> 6;
  const u8 g = ((c >> 5 & 0x3F) * 259 + 33) >> 6;
  const u8 b = ((c & 0x1F) * 527 + 23) >> 6;
  return (BcColor8888){r, g, b, 255};
}

/**
 * Pick the reference color that is closest in RGB space.
 */
INLINE_HINT static u8
bc_color_pick3(const BcColor8888 ref[PARAM_ARRAY_SIZE(4)], const BcColor8888 c) {
  u32 bestDistSqr = u32_max;
  u8  bestIndex;
  for (u8 i = 0; i != 4; ++i) {
    const u32 distSqr = bc_color_dist3_sqr(ref[i], c);
    if (distSqr < bestDistSqr) {
      bestDistSqr = distSqr;
      bestIndex   = i;
    }
  }
  return bestIndex;
}

INLINE_HINT static BcColor8888 bc_block_mean3(const Bc0Block* b) {
  u32 sumR = b->colors[0].r, sumG = b->colors[0].g, sumB = b->colors[0].b;
  for (u32 i = 1; i != 16; ++i) {
    sumR += b->colors[i].r;
    sumG += b->colors[i].g;
    sumB += b->colors[i].b;
  }
  return (BcColor8888){(u8)(sumR / 16), (u8)(sumG / 16), (u8)(sumB / 16), 255};
}

/**
 * Covariance matrix of a block.
 * NOTE: Only encodes covariance of the RGB components.
 */
typedef struct {
  f32 mat[6];
} BcBlockCovariance;

/**
 * Compute the covariance matrix of the colors in the block.
 */
INLINE_HINT static void bc_block_cov3(const Bc0Block* b, BcBlockCovariance* out) {
  const BcColor8888 mean = bc_block_mean3(b);

  i32 cov[6] = {0};
  for (u32 i = 0; i != 16; ++i) {
    const i32 dR = b->colors[i].r - mean.r;
    const i32 dG = b->colors[i].g - mean.g;
    const i32 dB = b->colors[i].b - mean.b;

    cov[0] += dR * dR;
    cov[1] += dR * dG;
    cov[2] += dR * dB;
    cov[3] += dG * dG;
    cov[4] += dG * dB;
    cov[5] += dB * dB;
  }

  for (u32 i = 0; i != 6; ++i) {
    static const f32 g_u8MaxInv = 1.0f / u8_max;
    out->mat[i]                 = cov[i] * g_u8MaxInv;
  }
}

INLINE_HINT static BcVec bc_block_cov3_mul(const BcBlockCovariance* c, const BcVec a) {
  const f32 x = a.x * c->mat[0] + a.y * c->mat[1] + a.z * c->mat[2];
  const f32 y = a.x * c->mat[1] + a.y * c->mat[3] + a.z * c->mat[4];
  const f32 z = a.x * c->mat[2] + a.y * c->mat[4] + a.z * c->mat[5];
  return (BcVec){x, y, z};
}

/**
 * Find the principle axis of the colors (rgb only) in a block using power iteration.
 */
INLINE_HINT static BcVec bc_block_principle_axis(const BcBlockCovariance* cov) {
  BcVec axis = {1, 1, 1};
  // Iteratively push the axis towards the principle axis.
  // NOTE: Keep itr count low as we don't normalize per itr so we can run into precision issues.
  const u32 powerItrs = 10;
  for (u32 i = 0; i != powerItrs; ++i) {
    axis = bc_block_cov3_mul(cov, axis);
  }
  const f32 max = bc_vec_max(axis);
  return max > f32_epsilon ? bc_vec_mul(axis, 1.0f / max) : (BcVec){1, 1, 1};
}

/**
 * Compute the endpoints of a line through RGB space that can be used to approximate the colors in
 * the given block.
 */
INLINE_HINT static void bc_block_color_fit(const Bc0Block* b, BcColor565* out0, BcColor565* out1) {
  BcBlockCovariance covariance;
  bc_block_cov3(b, &covariance);

  const BcVec principleAxis = bc_block_principle_axis(&covariance);

  /**
   * Find the min/max colors along the principle axis (axis that fits the most colors).
   * NOTE: In the future we could consider doing some kind of iterative refinement to find
   * end-points that cause the least error with all the block colors.
   */
  BcColor8888 minColor = b->colors[0], maxColor = b->colors[0];
  f32         minDot = bc_color_dot3(b->colors[0], principleAxis);
  f32         maxDot = minDot;
  for (u32 i = 1; i != 16; ++i) {
    const f32 dot = bc_color_dot3(b->colors[i], principleAxis);
    if (dot < minDot) {
      minDot   = dot;
      minColor = b->colors[i];
    }
    if (dot > maxDot) {
      maxDot   = dot;
      maxColor = b->colors[i];
    }
  }

  *out0 = bc_color_to_565(maxColor);
  *out1 = bc_color_to_565(minColor);
}

INLINE_HINT static const u8* bc_block_values_r(const Bc0Block* b) {
  return bits_ptr_offset(b->colors, offsetof(BcColor8888, r));
}

INLINE_HINT static u8* bc_block_values_mut_r(Bc0Block* b) {
  return bits_ptr_offset(b->colors, offsetof(BcColor8888, r));
}

INLINE_HINT static const u8* bc_block_values_a(const Bc0Block* b) {
  return bits_ptr_offset(b->colors, offsetof(BcColor8888, a));
}

INLINE_HINT static u8* bc_block_values_mut_a(Bc0Block* b) {
  return bits_ptr_offset(b->colors, offsetof(BcColor8888, a));
}

/**
 * Compute two middle points on the given line through RGB space.
 */
INLINE_HINT static void bc_line_color3_interpolate(BcColor8888 line[PARAM_ARRAY_SIZE(4)]) {
  /**
   * We use the bc1 mode that uses 2 interpolated implicit colors.
   *
   * Bc1 reference colors:
   * - RGB0: color0                (if color0 > color1)
   * - RGB1: color1                (if color0 > color1)
   * - RGB2: (2 * RGB0 + RGB1) / 3 (if color0 > color1)
   * - RGB3: (RGB0 + 2 * RGB1) / 3 (if color0 > color1)
   */
  line[2].r = (line[0].r * 2 + line[1].r * 1) / 3;
  line[2].g = (line[0].g * 2 + line[1].g * 1) / 3;
  line[2].b = (line[0].b * 2 + line[1].b * 1) / 3;
  line[2].a = 255;

  line[3].r = (line[0].r * 1 + line[1].r * 2) / 3;
  line[3].g = (line[0].g * 1 + line[1].g * 2) / 3;
  line[3].b = (line[0].b * 1 + line[1].b * 2) / 3;
  line[3].a = 255;
}

/**
 * Compute 6 middle points on the given line through 1D space.
 */
INLINE_HINT static void bc_line_value_interpolate(u8 line[PARAM_ARRAY_SIZE(8)]) {
  /**
   * We use the bc3/bc4 mode that uses 6 interpolated implicit values.
   *
   * Bc3/bc4 reference values:
   * - a0: value0                 (if value0 > value1)
   * - a1: value1                 (if value0 > value1)
   * - a2: (6 * a0 + 1 * a1 ) / 7 (if value0 > value1)
   * - a3: (5 * a0 + 2 * a1 ) / 7 (if value0 > value1)
   * - a4: (4 * a0 + 3 * a1 ) / 7 (if value0 > value1)
   * - a5: (3 * a0 + 4 * a1 ) / 7 (if value0 > value1)
   * - a6: (2 * a0 + 5 * a1 ) / 7 (if value0 > value1)
   * - a7: (1 * a0 + 6 * a1 ) / 7 (if value0 > value1)
   */
  line[2] = (line[0] * 6 + line[1] * 1 + 1) / 7;
  line[3] = (line[0] * 5 + line[1] * 2 + 1) / 7;
  line[4] = (line[0] * 4 + line[1] * 3 + 1) / 7;
  line[5] = (line[0] * 3 + line[1] * 4 + 1) / 7;
  line[6] = (line[0] * 2 + line[1] * 5 + 1) / 7;
  line[7] = (line[0] * 1 + line[1] * 6 + 1) / 7;
}

/**
 * For each color pick of one the reference colors and encode the 2-bit index.
 */
INLINE_HINT static void bc_colors_encode(
    const BcColor8888 colors[PARAM_ARRAY_SIZE(16)],
    const BcColor8888 ref[PARAM_ARRAY_SIZE(4)],
    u32*              outIndices) {
  *outIndices = 0;
  for (u32 i = 0; i != 16; ++i) {
    const u8 index = bc_color_pick3(ref, colors[i]);
    *outIndices |= index << (i * 2);
  }
}

INLINE_HINT static void bc_colors_decode(
    const BcColor8888 ref[PARAM_ARRAY_SIZE(4)],
    const u32         indices,
    BcColor8888       out[PARAM_ARRAY_SIZE(16)]) {
  for (u32 i = 0; i != 16; ++i) {
    const u8 index = (indices >> (i * 2)) & 0b11;
    out[i]         = ref[index];
  }
}

/**
 * Compute the endpoints of a line through 1D space that can be used to approximate the values in
 * the given block.
 */
INLINE_HINT static void bc_value_fit(const u8* values, const u32 valueStride, u8* out0, u8* out1) {
  u8 min = *values, max = *values;
  for (u32 i = 0; i != 15; ++i) {
    values += valueStride;
    min = math_min(min, *values);
    max = math_max(max, *values);
  }
  *out0 = max;
  *out1 = min;
}

/**
 * Map a linear index (0 min, 7 max, 1-6 interp) to a Bc value index (0 min, 1 max, 2-7 interp).
 */
INLINE_HINT static u8 bc_value_index_map(const u8 linearIndex) {
  // Clever bit-fiddling based on the STB implementation: https://github.com/nothings/stb/
  u8 res = -linearIndex & 7;
  res ^= res < 2;
  return res;
}

/**
 * For each value pick of one the 8 linearly interpolated values between min/max and encode the
 * 3-bit index.
 * NOTE: We only support the 8 value mode and not the 6 value + 0/255 mode at the moment.
 */
INLINE_HINT static void bc_value_encode(
    const u8* values,
    const u32 valueStride,
    const u8  min,
    const u8  max,
    u8        outIndices[PARAM_ARRAY_SIZE(6)]) {
  /**
   * Pick the exact closest of the 8 values based on the min/max, for details see:
   * https://fgiesen.wordpress.com/2009/12/15/dxt5-alpha-block-index-determination/
   */
  diag_assert(max > min);
  const u32 range = max - min;
  const u32 bias  = (range < 8) ? (range - 1) : (range / 2 + 2);

  u32 indexBuffer = 0, bitCount = 0;
  u8* outPtr = outIndices;
  for (u32 i = 0; i != 16; ++i, values += valueStride) {
    const u8 index = ((*values - min) * 7 + bias) / range;

    // Accumulate 3bit indices until we've filled up a byte and then output it.
    indexBuffer |= bc_value_index_map(index) << bitCount;
    if ((bitCount += 3) >= 8) {
      *outPtr++ = (u8)indexBuffer;
      indexBuffer >>= 8;
      bitCount -= 8;
    }
  }
}

INLINE_HINT static void bc_value_decode(
    const u8  ref[PARAM_ARRAY_SIZE(8)],
    const u8  indices[PARAM_ARRAY_SIZE(6)],
    u8*       outValues,
    const u32 outStride) {
  // Decode the 16 3bit indices.
  u64 indexStream = ((u64)indices[0] << 0) | ((u64)indices[1] << 8) | ((u64)indices[2] << 16) |
                    ((u64)indices[3] << 24) | ((u64)indices[4] << 32) | ((u64)indices[5] << 40);
  for (u32 i = 0; i != 16; ++i, indexStream >>= 3, outValues += outStride) {
    const u8 index = indexStream & 0x07;
    *outValues     = ref[index];
  }
}

void bc0_extract1(const u8* restrict in, const u32 width, Bc0Block* restrict out) {
  diag_assert_msg(bits_aligned(width, 4), "Width has to be a multiple of 4");

  for (u32 y = 0; y != 4; ++y, in += width) {
    for (u32 x = 0; x != 4; ++x) {
      out->colors[y * 4 + x] = (BcColor8888){in[x], 0, 0, 255};
    }
  }
}

void bc0_extract4(const BcColor8888* restrict in, const u32 width, Bc0Block* restrict out) {
  diag_assert_msg(bits_aligned(width, 4), "Width has to be a multiple of 4");

  for (u32 y = 0; y != 4; ++y, in += width) {
    for (u32 x = 0; x != 4; ++x) {
      out->colors[y * 4 + x] = in[x];
    }
  }
}

void bc0_scanout4(const Bc0Block* restrict in, const u32 width, BcColor8888* restrict out) {
  diag_assert_msg(bits_aligned(width, 4), "Width has to be a multiple of 4");

  for (u32 y = 0; y != 4; ++y, out += width) {
    for (u32 x = 0; x != 4; ++x) {
      *(out + x) = in->colors[y * 4 + x];
    }
  }
}

void bc1_encode(const Bc0Block* restrict in, Bc1Block* restrict out) {
  bc_block_color_fit(in, &out->color0, &out->color1);

  /**
   * To use the encoding mode with two interpolated colors we need to make sure that color0 is
   * always larger then color1.
   */
  if (out->color0 < out->color1) {
    const BcColor565 tmp = out->color0;
    out->color0          = out->color1;
    out->color1          = tmp;
  } else if (out->color0 == out->color1) {
    out->colorIndices = 0;
    return;
  }

  BcColor8888 refColors[4];
  refColors[0] = bc_color_from_565(out->color0);
  refColors[1] = bc_color_from_565(out->color1);
  bc_line_color3_interpolate(refColors);

  bc_colors_encode(in->colors, refColors, &out->colorIndices);
}

void bc1_decode(const Bc1Block* restrict in, Bc0Block* restrict out) {
  /**
   * NOTE: This only supports the bc1 mode with 2 interpolated implicit colors, and thus assumes
   * color0 is always greater then color1. When color0 is equal to color1 then we assume that only
   * one of the explicit colors is used and not one of the interpolated colors.
   */
  BcColor8888 refColors[4];
  refColors[0] = bc_color_from_565(in->color0);
  refColors[1] = bc_color_from_565(in->color1);
  bc_line_color3_interpolate(refColors);

  bc_colors_decode(refColors, in->colorIndices, out->colors);
}

void bc3_encode(const Bc0Block* restrict in, Bc3Block* restrict out) {
  bc_value_fit(bc_block_values_a(in), 4, &out->alpha0, &out->alpha1);

  if (out->alpha0 == out->alpha1) {
    mem_set(array_mem(out->alphaIndices), 0);
  } else {
    bc_value_encode(bc_block_values_a(in), 4, out->alpha1, out->alpha0, out->alphaIndices);
  }

  bc_block_color_fit(in, &out->color0, &out->color1);

  BcColor8888 refColors[4];
  refColors[0] = bc_color_from_565(out->color0);
  refColors[1] = bc_color_from_565(out->color1);
  bc_line_color3_interpolate(refColors);

  bc_colors_encode(in->colors, refColors, &out->colorIndices);
}

void bc3_decode(const Bc3Block* restrict in, Bc0Block* restrict out) {
  BcColor8888 refColors[4];
  refColors[0] = bc_color_from_565(in->color0);
  refColors[1] = bc_color_from_565(in->color1);
  bc_line_color3_interpolate(refColors);
  bc_colors_decode(refColors, in->colorIndices, out->colors);

  /**
   * NOTE: This only supports the bc3 alpha mode with 6 interpolated implicit values, and thus
   * assumes alpha0 is always greater then alpha1. When alpha0 is equal to alpha1 then we assume
   * that only one of the explicit values is used and not one of the interpolated values.
   */
  u8 refAlpha[8];
  refAlpha[0] = in->alpha0;
  refAlpha[1] = in->alpha1;
  bc_line_value_interpolate(refAlpha);
  bc_value_decode(refAlpha, in->alphaIndices, bc_block_values_mut_a(out), 4);
}

void bc4_encode(const Bc0Block* restrict in, Bc4Block* restrict out) {
  bc_value_fit(bc_block_values_r(in), 4, &out->value0, &out->value1);

  if (out->value0 == out->value1) {
    mem_set(array_mem(out->valueIndices), 0);
  } else {
    bc_value_encode(bc_block_values_r(in), 4, out->value1, out->value0, out->valueIndices);
  }
}

void bc4_decode(const Bc4Block* restrict in, Bc0Block* restrict out) {
  /**
   * NOTE: This only supports the bc4 mode with 6 interpolated implicit values, and thus assumes
   * value0 is always greater then value1. When value0 is equal to value1 then we assume that only
   * one of the explicit values is used and not one of the interpolated values.
   */
  u8 refValues[8];
  refValues[0] = in->value0;
  refValues[1] = in->value1;
  bc_line_value_interpolate(refValues);
  bc_value_decode(refValues, in->valueIndices, bc_block_values_mut_r(out), 4);
}
