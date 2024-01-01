#include "core_array.h"
#include "core_bc.h"
#include "core_bits.h"
#include "core_diag.h"

/**
 * Texture Block Compression.
 *
 * Specification:
 * https://registry.khronos.org/DataFormat/specs/1.3/dataformat.1.3.html#S3TC
 * https://learn.microsoft.com/en-us/windows/win32/direct3d10/d3d10-graphics-programming-guide-resources-block-compression
 *
 * References:
 * 'Real-Time DXT Compression by J.M.P. van Waveren, 2006, Id Software, Inc.':
 * https://www.researchgate.net/publication/259000525_Real-Time_DXT_Compression
 *
 * NOTE: This encoder assumes a little-endian host system.
 */

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

static u32 bc_color_luminance(const BcColor8888 c) { return c.r + c.g * 2 + c.b; }

static u32 bc_color_distance_sqr(const BcColor8888 a, const BcColor8888 b) {
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

static u8 bc_color_pick(const BcColor8888 ref[PARAM_ARRAY_SIZE(4)], const BcColor8888 c) {
  /**
   * Pick the reference color that is closest in RGB space.
   */
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

static void bc_block_implicit_colors(
    const BcColor8888 min, const BcColor8888 max, BcColor8888* outA, BcColor8888* outB) {
  /**
   * We use the bc1 mode that uses 2 interpolated implicit colors.
   *
   * Bc1 reference colors:
   * - RGB0: color0                (if color0 > color1)
   * - RGB1: color1                (if color0 > color1)
   * - RGB2: (2 * RGB0 + RGB1) / 3 (if color0 > color1)
   * - RGB3: (RGB0 + 2 * RGB1) / 3 (if color0 > color1)
   */
  outA->r = (min.r * 2 + max.r * 1) / 3;
  outA->g = (min.g * 2 + max.g * 1) / 3;
  outA->b = (min.b * 2 + max.b * 1) / 3;
  outB->r = (min.r * 1 + max.r * 2) / 3;
  outB->g = (min.g * 1 + max.g * 2) / 3;
  outB->b = (min.b * 1 + max.b * 2) / 3;
}

static void bc_block_bounds(const Bc0Block* b, BcColor8888* outMin, BcColor8888* outMax) {
  /**
   * Find the color with the lowest and the color with the highest luminance.
   */
  u32 lumMin = u32_max, lumMax = 0;
  array_for_t(b->colors, BcColor8888, c) {
    const u32 lum = bc_color_luminance(*c);
    if (lum >= lumMax) {
      lumMax  = lum;
      *outMax = *c;
    }
    if (lum <= lumMin) {
      lumMin  = lum;
      *outMin = *c;
    }
  }
}

void bc0_extract(const BcColor8888* in, const u32 width, Bc0Block* out) {
  diag_assert_msg(bits_aligned(width, 4), "Width has to be a multiple of 4");

  for (u32 y = 0; y != 4; ++y, in += width) {
    for (u32 x = 0; x != 4; ++x) {
      out->colors[y * 4 + x] = in[x];
    }
  }
}

void bc0_scanout(const Bc0Block* in, const u32 width, BcColor8888* out) {
  diag_assert_msg(bits_aligned(width, 4), "Width has to be a multiple of 4");

  for (u32 y = 0; y != 4; ++y, out += width) {
    for (u32 x = 0; x != 4; ++x) {
      *(out + x) = in->colors[y * 4 + x];
    }
  }
}

void bc1_encode(const Bc0Block* in, Bc1Block* out) {
  BcColor8888 min, max;
  bc_block_bounds(in, &min, &max);

  /**
   * To use the encoding mode with two interpolated colors we need to make sure that color0 is
   * always larger then color1.
   * NOTE: When color0 is equal to color1 we do end up using the mode where the 4th color is black
   * instead of an interpolated value, this should not be a problem however as when min is equal to
   * max then all colors must be equal so we can use index 0 for all entries.
   */
  if (bc_color_to_565(max) < bc_color_to_565(min)) {
    bc_color_swap(&min, &max);
  }

  BcColor8888 refColors[4];
  refColors[0] = bc_color_quantize_565(max);
  refColors[1] = bc_color_quantize_565(min);
  bc_block_implicit_colors(refColors[0], refColors[1], &refColors[2], &refColors[3]);

  out->color0  = bc_color_to_565(max);
  out->color1  = bc_color_to_565(min);
  out->indices = 0;
  for (u32 i = 0; i != array_elems(in->colors); ++i) {
    const u8 index = bc_color_pick(refColors, in->colors[i]);
    out->indices |= index << (i * 2);
  }
}

void bc1_decode(const Bc1Block* in, Bc0Block* out) {
  /**
   * NOTE: This only supports the bc1 mode with 2 interpolated implicit colors, and thus assumes
   * color0 is always greater then color1. When color0 is equal to color1 then we assume that only
   * one of the explicit colors is used and not one of the interpolated colors.
   */
  BcColor8888 refColors[4];
  refColors[0] = bc_color_from_565(in->color0);
  refColors[1] = bc_color_from_565(in->color1);
  bc_block_implicit_colors(refColors[0], refColors[1], &refColors[2], &refColors[3]);

  for (u32 i = 0; i != array_elems(out->colors); ++i) {
    const u8 index = (in->indices >> (i * 2)) & 0b11;
    out->colors[i] = refColors[index];
  }
}
