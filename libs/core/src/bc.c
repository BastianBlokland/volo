#include "core_array.h"
#include "core_bc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"

/**
 * Texture Block Compression.
 *
 * Specification:
 * https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.html#S3TC
 * https://microsoft.github.io/DirectX-Specs/d3d/archive/D3D11_3_FunctionalSpec.htm#BCFormats
 *
 * References:
 * https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-block-compression
 * 'Real-Time DXT Compression by J.M.P. van Waveren, 2006, Id Software, Inc.':
 *     https://www.researchgate.net/publication/259000525_Real-Time_DXT_Compression
 * https://fgiesen.wordpress.com/2021/10/04/gpu-bcn-decoding/
 *
 * NOTE: This encoder assumes a little-endian host system.
 */

#define bc_line_fit_use_inset

static BcColor565 bc_color_to_565(const BcColor8888 c) {
  const u16 r = ((c.r >> 3) & 0x1F) << 11;
  const u16 g = ((c.g >> 2) & 0x3F) << 5;
  const u16 b = (c.b >> 3) & 0x1F;
  return (BcColor565){r | g | b};
}

static BcColor8888 bc_color_from_565(const BcColor565 c) {
  const u8 r = (c & 0xF800) >> 8;
  const u8 g = (c & 0x07E0) >> 3;
  const u8 b = (c & 0x001F) << 3;
  return (BcColor8888){r, g, b, 255};
}

/**
 * Quantize a color in the same way that converting it to 565 and back would do.
 */
static BcColor8888 bc_color_quantize_565(const BcColor8888 c) {
  const u8 r = (c.r & 0xF8) | (c.r >> 5);
  const u8 g = (c.g & 0xFC) | (c.g >> 6);
  const u8 b = (c.b & 0xF8) | (c.b >> 5);
  return (BcColor8888){r, g, b, 255};
}

static u32 bc_color_distance_sqr(const BcColor8888 a, const BcColor8888 b) {
  const i32 dR = b.r - a.r;
  const i32 dG = b.g - a.g;
  const i32 dB = b.b - a.b;
  return dR * dR + dG * dG + dB * dB;
}

/**
 * Pick the reference color that is closest in RGB space.
 */
static u8 bc_color_pick(const BcColor8888 ref[PARAM_ARRAY_SIZE(4)], const BcColor8888 c) {
  u32 bestDistSqr = u32_max;
  u8  bestIndex;
  for (u8 i = 0; i != 4; ++i) {
    const u32 distSqr = bc_color_distance_sqr(ref[i], c);
    if (distSqr < bestDistSqr) {
      bestDistSqr = distSqr;
      bestIndex   = i;
    }
  }
  return bestIndex;
}

MAYBE_UNUSED static void bc_line_inset(BcColor8888* start, BcColor8888* end) {
  BcColor8888 inset;
  inset.r = (end->r - start->r) / 16;
  inset.g = (end->g - start->g) / 16;
  inset.b = (end->b - start->b) / 16;

  start->r = (start->r + inset.r <= 255) ? start->r + inset.r : 255;
  start->g = (start->g + inset.g <= 255) ? start->g + inset.g : 255;
  start->b = (start->b + inset.b <= 255) ? start->b + inset.b : 255;

  end->r = (end->r >= inset.r) ? end->r - inset.r : 0;
  end->g = (end->g >= inset.g) ? end->g - inset.g : 0;
  end->b = (end->b >= inset.b) ? end->b - inset.b : 0;
}

typedef struct {
  BcColor8888 min, max, mean;
} BcBlockAnalysis;

static BcBlockAnalysis bc_block_analyze(const Bc0Block* b) {
  BcBlockAnalysis res;
  res.min = res.max = res.mean = b->colors[0];

  for (u32 i = 1; i != 16; ++i) {
    res.min.r = math_min(res.min.r, b->colors[i].r);
    res.min.g = math_min(res.min.g, b->colors[i].g);
    res.min.b = math_min(res.min.b, b->colors[i].b);

    res.max.r = math_max(res.max.r, b->colors[i].r);
    res.max.g = math_max(res.max.g, b->colors[i].g);
    res.max.b = math_max(res.max.b, b->colors[i].b);

    res.mean.r += b->colors[i].r;
    res.mean.g += b->colors[i].g;
    res.mean.b += b->colors[i].b;
  }

  // NOTE: + 8 to round to nearest.
  res.mean.r = (res.mean.r + 8) / 16;
  res.mean.g = (res.mean.g + 8) / 16;
  res.mean.b = (res.mean.b + 8) / 16;

  return res;
}

/**
 * Compute the endpoints of a line through RGB space that can be used to approximate the colors in
 * the given block.
 */
static void bc_block_line_fit(const Bc0Block* b, BcColor8888* outC0, BcColor8888* outC1) {
  const BcBlockAnalysis analysis = bc_block_analyze(b);
  *outC0                         = analysis.max;
  *outC1                         = analysis.min;

#ifdef bc_line_fit_use_inset
  /**
   * Slightly insetting the bounds results in a bit more error at the extreme edges of the block but
   * less error in between, this can be a good tradeoff.
   */
  bc_line_inset(outC1, outC0);
#endif
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
