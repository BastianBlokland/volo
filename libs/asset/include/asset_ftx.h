#pragma once
#include "core_unicode.h"
#include "ecs_module.h"

/**
 * FontTeXture is a combination of a sdf (signed-distance-field) texture-atlas and a mapping from
 * unicode to glyph meta information.
 *
 * The texture atlas is a normal AssetTextureComp (from 'asset_texture.h') containing a signed
 * distance to the glyph border:
 * 0.0 = Well into the glyph.
 * 0.5 = Precisely on the border of the glyph.
 * 1.0 = Well outside the glyph.
 */

typedef struct {
  Unicode cp;
  u8      variation;
  u32     glyphIndex; // sentinel_u32 when character has no glyph (for example a space).
  f32     size;
  f32     offsetX, offsetY;
  f32     advance;
  f32     border;
} AssetFtxChar;

ecs_comp_extern_public(AssetFtxComp) {
  u32           glyphsPerDim;
  f32           lineSpacing;
  AssetFtxChar* characters; // Sorted on the unicode codepoint.
  usize         characterCount;
};

/**
 * Get a character based on a unicode codepoint.
 */
const AssetFtxChar* asset_ftx_lookup(const AssetFtxComp*, Unicode, u8 variation);
