#pragma once
#include "core_annotation.h"
#include "core_types.h"

/**
 * Texture Block Compression.
 * https://en.wikipedia.org/wiki/S3_Texture_Compression
 */

typedef struct {
  u8 r, g, b, a;
} BcColor8888;

ASSERT(sizeof(BcColor8888) == 4, "Unexpected rgba8888 size");

typedef u16 BcColor565;

typedef struct {
  ALIGNAS(16) BcColor8888 colors[16];
} Bc0Block;

ASSERT(alignof(Bc0Block) == 16, "Unexpected bc0 block alignment");
ASSERT(sizeof(Bc0Block) == 64, "Unexpected bc0 block size");

typedef struct {
  BcColor565 color0, color1;
  u32        indices;
} Bc1Block;

ASSERT(sizeof(Bc1Block) == 8, "Unexpected bc1 block size");

/**
 * Extract / scanout a single 4x4 BC0 (aka raw pixels) block.
 * Pre-condition: Width (and also height) input pixels have to be multiples of 4.
 */
void bc0_extract(const BcColor8888* in, u32 width, Bc0Block* out);
void bc0_scanout(const Bc0Block* in, u32 width, BcColor8888* out);

/**
 * Encode / decode a single 4x4 BC1 (aka S3TC DXT1) block.
 */
void bc1_encode(const Bc0Block* in, Bc1Block* out);
void bc1_decode(const Bc1Block* in, Bc0Block* out);
