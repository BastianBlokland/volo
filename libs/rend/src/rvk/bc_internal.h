#pragma once
#include "core_annotation.h"
#include "core_types.h"

/**
 * Texture Block Compression.
 * https://en.wikipedia.org/wiki/S3_Texture_Compression
 */

typedef struct {
  u8 r, g, b, a;
} RvkBcColor8888;

ASSERT(sizeof(RvkBcColor8888) == 4, "Unexpected rgba8888 size");

typedef u16 RvkBcColor565;

typedef struct {
  RvkBcColor8888 colors[16];
} RvkBc0Block;

ASSERT(sizeof(RvkBc0Block) == 64, "Unexpected bc0 block size");

typedef struct {
  RvkBcColor565 color0, color1;
  u32           indices;
} RvkBc1Block;

ASSERT(sizeof(RvkBc1Block) == 8, "Unexpected bc1 block size");

/**
 * Encode a single 4x4 BC1 (aka S3TC DXT1) block.
 */
void rvk_bc1_encode_block(const RvkBc0Block* in, RvkBc1Block* out);
