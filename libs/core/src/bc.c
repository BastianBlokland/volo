#include "core_array.h"
#include "core_bc.h"
#include "core_bits.h"
#include "core_diag.h"
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

INLINE_HINT static BcVec bc_color_to_vec(const BcColor8888 c) {
  static const f32 g_u8MaxInv = 1.0f / u8_max;
  return (BcVec){(f32)c.r * g_u8MaxInv, (f32)c.g * g_u8MaxInv, (f32)c.b * g_u8MaxInv};
}

INLINE_HINT static u32 bc_color_dist_sqr(const BcColor8888 a, const BcColor8888 b) {
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
bc_color_pick(const BcColor8888 ref[PARAM_ARRAY_SIZE(4)], const BcColor8888 c) {
  u32 bestDistSqr = u32_max;
  u8  bestIndex;
  for (u8 i = 0; i != 4; ++i) {
    const u32 distSqr = bc_color_dist_sqr(ref[i], c);
    if (distSqr < bestDistSqr) {
      bestDistSqr = distSqr;
      bestIndex   = i;
    }
  }
  return bestIndex;
}

INLINE_HINT static BcColor8888 bc_block_mean(const Bc0Block* b) {
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
 */
typedef struct {
  f32 mat[6];
} BcBlockCovariance;

/**
 * Compute the covariance matrix of the colors in the block.
 */
INLINE_HINT static void bc_block_cov(const Bc0Block* b, BcBlockCovariance* out) {
  const BcColor8888 mean = bc_block_mean(b);

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

INLINE_HINT static BcVec bc_block_cov_mul(const BcBlockCovariance* c, const BcVec a) {
  const f32 x = a.x * c->mat[0] + a.y * c->mat[1] + a.z * c->mat[2];
  const f32 y = a.x * c->mat[1] + a.y * c->mat[3] + a.z * c->mat[4];
  const f32 z = a.x * c->mat[2] + a.y * c->mat[4] + a.z * c->mat[5];
  return (BcVec){x, y, z};
}

/**
 * Find the principle axis of the colors in a block using power iteration.
 */
INLINE_HINT static BcVec bc_block_principle_axis(const BcBlockCovariance* cov) {
  BcVec axis = {1, 1, 1};
  // Iteratively push the axis towards the principle axis.
  // NOTE: Keep itr count low as we don't normalize per itr so we can run into precision issues.
  const u32 powerItrs = 10;
  for (u32 i = 0; i != powerItrs; ++i) {
    axis = bc_block_cov_mul(cov, axis);
  }
  return bc_vec_mul(axis, 1.0f / bc_vec_max(axis));
}

INLINE_HINT static void
bc_block_min_max(const Bc0Block* b, const BcVec axis, BcColor8888* outMin, BcColor8888* outMax) {
  *outMin    = b->colors[0];
  *outMax    = b->colors[0];
  f32 minDot = bc_color_dot3(b->colors[0], axis);
  f32 maxDot = minDot;

  for (u32 i = 1; i != 16; ++i) {
    const f32 dot = bc_color_dot3(b->colors[i], axis);
    if (dot < minDot) {
      minDot  = dot;
      *outMin = b->colors[i];
    }
    if (dot > maxDot) {
      maxDot  = dot;
      *outMax = b->colors[i];
    }
  }
}

/**
 * Compute the endpoints of a line through RGB space that can be used to approximate the colors in
 * the given block.
 */
INLINE_HINT static void bc_block_fit(const Bc0Block* b, BcColor565* outC0, BcColor565* outC1) {
  BcBlockCovariance covariance;
  bc_block_cov(b, &covariance);

  const BcVec principleAxis = bc_block_principle_axis(&covariance);

  BcColor8888 min, max;
  bc_block_min_max(b, principleAxis, &min, &max);

  *outC0 = bc_color_to_565(min);
  *outC1 = bc_color_to_565(max);
}

/**
 * Compute two middle points on the given line through RGB space.
 */
INLINE_HINT static void bc_block_line_interpolate(
    const BcColor8888 c0, const BcColor8888 c1, BcColor8888* outC2, BcColor8888* outC3) {
  /**
   * We use the bc1 mode that uses 2 interpolated implicit colors.
   *
   * Bc1 reference colors:
   * - RGB0: color0                (if color0 > color1)
   * - RGB1: color1                (if color0 > color1)
   * - RGB2: (2 * RGB0 + RGB1) / 3 (if color0 > color1)
   * - RGB3: (RGB0 + 2 * RGB1) / 3 (if color0 > color1)
   */
  outC2->r = (c0.r * 2 + c1.r * 1) / 3;
  outC2->g = (c0.g * 2 + c1.g * 1) / 3;
  outC2->b = (c0.b * 2 + c1.b * 1) / 3;
  outC2->a = 255;

  outC3->r = (c0.r * 1 + c1.r * 2) / 3;
  outC3->g = (c0.g * 1 + c1.g * 2) / 3;
  outC3->b = (c0.b * 1 + c1.b * 2) / 3;
  outC3->a = 255;
}

void bc0_extract(const BcColor8888* restrict in, const u32 width, Bc0Block* restrict out) {
  diag_assert_msg(bits_aligned(width, 4), "Width has to be a multiple of 4");

  for (u32 y = 0; y != 4; ++y, in += width) {
    for (u32 x = 0; x != 4; ++x) {
      out->colors[y * 4 + x] = in[x];
    }
  }
}

void bc0_scanout(const Bc0Block* restrict in, const u32 width, BcColor8888* restrict out) {
  diag_assert_msg(bits_aligned(width, 4), "Width has to be a multiple of 4");

  for (u32 y = 0; y != 4; ++y, out += width) {
    for (u32 x = 0; x != 4; ++x) {
      *(out + x) = in->colors[y * 4 + x];
    }
  }
}

void bc1_encode(const Bc0Block* restrict in, Bc1Block* restrict out) {
  BcColor565 color0, color1;
  bc_block_fit(in, &color0, &color1);

  /**
   * To use the encoding mode with two interpolated colors we need to make sure that color0 is
   * always larger then color1.
   */
  if (UNLIKELY(color0 < color1)) {
    const BcColor565 tmp = color0;
    color0               = color1;
    color1               = tmp;
  } else if (UNLIKELY(color0 == color1)) {
    out->color0  = color0;
    out->color1  = color0;
    out->indices = 0;
    return;
  }

  BcColor8888 refColors[4];
  refColors[0] = bc_color_from_565(color0);
  refColors[1] = bc_color_from_565(color1);
  bc_block_line_interpolate(refColors[0], refColors[1], &refColors[2], &refColors[3]);

  out->color0  = color0;
  out->color1  = color1;
  out->indices = 0;
  for (u32 i = 0; i != array_elems(in->colors); ++i) {
    const u8 index = bc_color_pick(refColors, in->colors[i]);
    out->indices |= index << (i * 2);
  }
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
  bc_block_line_interpolate(refColors[0], refColors[1], &refColors[2], &refColors[3]);

  for (u32 i = 0; i != array_elems(out->colors); ++i) {
    const u8 index = (in->indices >> (i * 2)) & 0b11;
    out->colors[i] = refColors[index];
  }
}
