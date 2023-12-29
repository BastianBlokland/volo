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
 */

static BcColor565 bc_color_to_565(const BcColor8888* c) {
  const u16 r = ((c->r >> 3) & 0x1f) << 11;
  const u16 g = ((c->g >> 2) & 0x3f) << 5;
  const u16 b = (c->b >> 3) & 0x1f;
  return (u16)(r | g | b);
}

/**
 * Quantize a color in the same way that converting it to 565 and back would do.
 */
static void bc_color_quantize_565(BcColor8888* c) {
  c->r = (c->r & 0b11111000) | (c->r >> 5);
  c->g = (c->g & 0b11111100) | (c->g >> 6);
  c->b = (c->b & 0b11111000) | (c->b >> 5);
}

static u32 bc_color_luminance(const BcColor8888* c) { return c->r + c->g * 2 + c->b; }

static u32 bc_color_distance_sqr(const BcColor8888* a, const BcColor8888* b) {
  const i32 dR = a->r - b->r;
  const i32 dG = a->g - b->g;
  const i32 dB = a->b - b->b;
  return dR * dR + dG * dG + dB * dB;
}

static void bc_color_swap(BcColor8888* a, BcColor8888* b) {
  BcColor8888 tmp = *a;
  *a              = *b;
  *b              = tmp;
}

static u8 bc_color_pick(const BcColor8888 ref[PARAM_ARRAY_SIZE(4)], const BcColor8888* c) {
  u32 bestDistSqr = u32_max;
  u8  bestIndex;
  for (u8 i = 0; i != 4; ++i) {
    const u32 distSqr = bc_color_distance_sqr(c, &ref[i]);
    if (distSqr < bestDistSqr) {
      bestDistSqr = distSqr;
      bestIndex   = i;
    }
  }
  return bestIndex;
}

static void bc_block_implicit_colors(
    const BcColor8888* min, const BcColor8888* max, BcColor8888* outA, BcColor8888* outB) {
  outA->r = (min->r * 2 + max->r * 1) / 3;
  outA->g = (min->g * 2 + max->g * 1) / 3;
  outA->b = (min->b * 2 + max->b * 1) / 3;
  outB->r = (min->r * 1 + max->r * 2) / 3;
  outB->g = (min->g * 1 + max->g * 2) / 3;
  outB->b = (min->b * 1 + max->b * 2) / 3;
}

static void bc_block_bounds(const Bc0Block* b, BcColor8888* outMin, BcColor8888* outMax) {
  u32 lumMin = u32_max, lumMax = 0;
  array_for_t(b->colors, BcColor8888, c) {
    const u32 lum = bc_color_luminance(c);
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

void bc1_encode(const Bc0Block* in, Bc1Block* out) {
  BcColor8888 min, max;
  bc_block_bounds(in, &min, &max);

  if (bc_color_to_565(&max) < bc_color_to_565(&min)) {
    bc_color_swap(&min, &max);
  }

  BcColor8888 refColors[4];
  refColors[0] = min;
  refColors[1] = max;

  bc_color_quantize_565(&refColors[0]);
  bc_color_quantize_565(&refColors[1]);

  bc_block_implicit_colors(&refColors[0], &refColors[1], &refColors[2], &refColors[3]);

  out->color0  = bc_color_to_565(&max);
  out->color1  = bc_color_to_565(&min);
  out->indices = 0;
  for (u32 i = 0; i != array_elems(in->colors); ++i) {
    const u8 index = bc_color_pick(&in->colors[i], refColors);
    out->indices |= index << (i << 1);
  }
}
