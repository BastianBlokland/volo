#pragma once
#include "core.h"

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
  u32        colorIndices; // 4x4 lookup table with 2 bit indices.
} Bc1Block;

ASSERT(sizeof(Bc1Block) == 8, "Unexpected bc1 block size");

typedef struct {
  u8         alpha0, alpha1;
  u8         alphaIndices[6]; // 4x4 lookup table with 3 bit indices.
  BcColor565 color0, color1;
  u32        colorIndices; // 4x4 lookup table with 2 bit indices.
} Bc3Block;

ASSERT(sizeof(Bc3Block) == 16, "Unexpected bc3 block size");

typedef struct {
  u8 value0, value1;
  u8 valueIndices[6]; // 4x4 lookup table with 3 bit indices.
} Bc4Block;

ASSERT(sizeof(Bc4Block) == 8, "Unexpected bc4 block size");

/**
 * Extract / scanout a single 4x4 BC0 (aka raw pixels) block.
 * Pre-condition: Width (and also height) input pixels have to be multiples of 4.
 */
void bc0_extract(const u8* restrict in, u32 channels, u32 width, Bc0Block* restrict out);
void bc0_extract4(const BcColor8888* restrict in, u32 width, Bc0Block* restrict out);
void bc0_scanout4(const Bc0Block* restrict in, u32 width, BcColor8888* restrict out);

/**
 * Encode / decode a single 4x4 BC1 (aka S3TC DXT1) (RGB) block.
 */
void bc1_encode(const Bc0Block* restrict in, Bc1Block* restrict out);
void bc1_decode(const Bc1Block* restrict in, Bc0Block* restrict out);

/**
 * Encode / decode a single 4x4 BC3 (aka S3TC DXT4 / DXT5) (RGBA) block.
 */
void bc3_encode(const Bc0Block* restrict in, Bc3Block* restrict out);
void bc3_decode(const Bc3Block* restrict in, Bc0Block* restrict out);

/**
 * Encode / decode a single 4x4 BC4 (R) block.
 */
void bc4_encode(const Bc0Block* restrict in, Bc4Block* restrict out);
void bc4_decode(const Bc4Block* restrict in, Bc0Block* restrict out);
