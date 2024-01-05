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

#define bc_pca_power_itrs 4

typedef struct {
  f32 vals[3];
} BcAxis;

static BcAxis bc_axis_luminance() {
  // Luminance (brightness) coefficients.
  return (BcAxis){.vals[0] = 0.299f, .vals[1] = 0.587f, .vals[2] = 0.114f};
}

static f32 bc_axis_dot(const BcAxis a, const BcAxis b) {
  f32 res = 0;
  res += a.vals[0] * b.vals[0];
  res += a.vals[1] * b.vals[1];
  res += a.vals[2] * b.vals[2];
  return res;
}

static f32 bc_axis_mag_sqr(const BcAxis a) { return bc_axis_dot(a, a); }

static BcAxis bc_axis_div(const BcAxis a, const f32 scalar) {
  return (BcAxis){
      .vals[0] = a.vals[0] / scalar,
      .vals[1] = a.vals[1] / scalar,
      .vals[2] = a.vals[2] / scalar,
  };
}

static BcAxis bc_axis_sub(const BcAxis a, const BcAxis b) {
  return (BcAxis){
      .vals[0] = a.vals[0] - b.vals[0],
      .vals[1] = a.vals[1] - b.vals[1],
      .vals[2] = a.vals[2] - b.vals[2],
  };
}

static BcAxis bc_color_to_axis(const BcColor8888 c) {
  return (BcAxis){.vals[0] = c.r, .vals[1] = c.g, .vals[2] = c.b};
}

/**
 * Convert a 888 color to 565 with proper rounding.
 * Constants have been derived by 'Anonymous':
 * https://stackoverflow.com/questions/2442576/how-does-one-convert-16-bit-rgb565-to-24-bit-rgb888
 */
static BcColor565 bc_color_to_565(const BcColor8888 c) {
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
static BcColor8888 bc_color_from_565(const BcColor565 c) {
  const u8 r = ((c >> 11 & 0x1F) * 527 + 23) >> 6;
  const u8 g = ((c >> 5 & 0x3F) * 259 + 33) >> 6;
  const u8 b = ((c & 0x1F) * 527 + 23) >> 6;
  return (BcColor8888){r, g, b, 255};
}

/**
 * Quantize a color in the same way that converting it to 565 and back would do.
 */
static BcColor8888 bc_color_quantize_565(const BcColor8888 c) {
  // TODO: Investigate if this can be simplified (its a combination of to_565 and from_565).
  const u8 r = (((c.r * 249 + 1014) >> 11) * 527 + 23) >> 6;
  const u8 g = (((c.g * 253 + 505) >> 10) * 259 + 33) >> 6;
  const u8 b = (((c.b * 249 + 1014) >> 11) * 527 + 23) >> 6;
  return (BcColor8888){r, g, b, 255};
}

static u32 bc_color_dist_sqr(const BcColor8888 a, const BcColor8888 b) {
  const i32 dR = b.r - a.r;
  const i32 dG = b.g - a.g;
  const i32 dB = b.b - a.b;
  return dR * dR + dG * dG + dB * dB;
}

static void bc_color_swap(BcColor8888* a, BcColor8888* b) {
  BcColor8888 tmp = *a;
  *a              = *b;
  *b              = tmp;
}

/**
 * Pick the reference color that is closest in RGB space.
 */
static u8 bc_color_pick(const BcColor8888 ref[PARAM_ARRAY_SIZE(4)], const BcColor8888 c) {
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

typedef struct {
  BcColor8888 min, max, mean;
} BcBlockAnalysis;

static void bc_block_analyze(const Bc0Block* b, BcBlockAnalysis* out) {
  out->min = out->max = out->mean = b->colors[0];

  for (u32 i = 1; i != 16; ++i) {
    out->min.r = math_min(out->min.r, b->colors[i].r);
    out->min.g = math_min(out->min.g, b->colors[i].g);
    out->min.b = math_min(out->min.b, b->colors[i].b);

    out->max.r = math_max(out->max.r, b->colors[i].r);
    out->max.g = math_max(out->max.g, b->colors[i].g);
    out->max.b = math_max(out->max.b, b->colors[i].b);

    out->mean.r += b->colors[i].r;
    out->mean.g += b->colors[i].g;
    out->mean.b += b->colors[i].b;
  }

  // NOTE: +8 to round to nearest.
  out->mean.r = (out->mean.r + 8) / 16;
  out->mean.g = (out->mean.g + 8) / 16;
  out->mean.b = (out->mean.b + 8) / 16;
}

/**
 * Covariance matrix of a block.
 *
 * NOTE: Only the bottom left side of the matrix is computed, this is sufficient as the diagonal
 * is the covariance with itself and the top right is a mirror of the bottom left.
 *
 *  0 0 0 0
 *  1 0 0 0
 *  1 1 0 0
 *  1 1 1 0
 */
typedef struct {
  f32 vals[6];
} BcBlockCovariance;

/**
 * Compute the covariance matrix of the colors in the block.
 */
static void bc_block_cov(const Bc0Block* b, const BcColor8888 mean, BcBlockCovariance* out) {
  i32 mat[6] = {0};
  for (u32 i = 0; i != 16; ++i) {
    const i32 dR = b->colors[i].r - mean.r;
    const i32 dG = b->colors[i].g - mean.g;
    const i32 dB = b->colors[i].b - mean.b;

    mat[0] += dR * dR;
    mat[1] += dR * dG;
    mat[2] += dR * dB;
    mat[3] += dG * dG;
    mat[4] += dG * dB;
    mat[5] += dB * dB;
  }
  // Output the covariance matrix as floats.
  static const f32 g_u8MaxInv = 1.0f / u8_max;
  for (u32 i = 0; i != 6; ++i) {
    out->vals[i] = mat[i] * g_u8MaxInv;
  }
}

static BcAxis bc_block_cov_mul(const BcBlockCovariance* c, const BcAxis a) {
  const f32 x = a.vals[0] * c->vals[0] + a.vals[1] * c->vals[1] + a.vals[2] * c->vals[2];
  const f32 y = a.vals[0] * c->vals[1] + a.vals[1] * c->vals[3] + a.vals[2] * c->vals[4];
  const f32 z = a.vals[0] * c->vals[2] + a.vals[1] * c->vals[4] + a.vals[2] * c->vals[5];
  return (BcAxis){.vals[0] = x, .vals[1] = y, .vals[2] = z};
}

/**
 * Find the principle axis of the colors in a block using power iteration.
 */
static BcAxis bc_block_principle_axis(const BcBlockAnalysis* a, const BcBlockCovariance* cov) {
  // Start the axis in the extreme direction of the bounds.
  BcAxis axis = bc_axis_sub(bc_color_to_axis(a->max), bc_color_to_axis(a->min));

  // Iteratively push the axis towards the principle axis.
  // NOTE: We do not normalize per iteration so on many iterations the axis will get very large.
  for (u32 i = 0; i != bc_pca_power_itrs; ++i) {
    axis = bc_block_cov_mul(cov, axis);
  }

  const f32 magSqr = bc_axis_mag_sqr(axis);
  if (magSqr < 1.0f) {
    return bc_axis_luminance();
  }
  const f32 mag = intrinsic_sqrt_f32(magSqr);
  return bc_axis_div(axis, mag);
}

static void
bc_block_min_max(const Bc0Block* b, const BcAxis axis, BcColor8888* outMin, BcColor8888* outMax) {
  *outMin    = b->colors[0];
  *outMax    = b->colors[0];
  f32 minDot = bc_axis_dot(bc_color_to_axis(b->colors[0]), axis);
  f32 maxDot = minDot;

  for (u32 i = 1; i != 16; ++i) {
    const f32 dot = bc_axis_dot(bc_color_to_axis(b->colors[i]), axis);
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
static void bc_block_line_fit(const Bc0Block* b, BcColor8888* outC0, BcColor8888* outC1) {
  BcBlockAnalysis analysis;
  bc_block_analyze(b, &analysis);

  BcBlockCovariance covariance;
  bc_block_cov(b, analysis.mean, &covariance);

  const BcAxis principleAxis = bc_block_principle_axis(&analysis, &covariance);

  bc_block_min_max(b, principleAxis, outC1, outC0);

  /**
   * To use the encoding mode with two interpolated colors we need to make sure that color0 is
   * always larger then color1.
   * NOTE: When color0 is equal to color1 we do end up using the mode where the 4th color is black
   * instead of an interpolated value, this should not be a problem however as when min is equal to
   * max then all colors must be equal so we can use index 0 for all entries.
   */
  if (bc_color_to_565(*outC0) < bc_color_to_565(*outC1)) {
    bc_color_swap(outC1, outC0);
  }
}

/**
 * Compute two middle points on the given line through RGB space.
 */
static void bc_block_line_interpolate(
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
  BcColor8888 color0, color1;
  bc_block_line_fit(in, &color0, &color1);

  BcColor8888 refColors[4];
  refColors[0] = bc_color_quantize_565(color0);
  refColors[1] = bc_color_quantize_565(color1);
  bc_block_line_interpolate(refColors[0], refColors[1], &refColors[2], &refColors[3]);

  out->color0  = bc_color_to_565(color0);
  out->color1  = bc_color_to_565(color1);
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
