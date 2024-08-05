#pragma once
#include "core_unicode.h"
#include "data_registry.h"
#include "ecs_module.h"

/**
 * FontTexture is a combination of a sdf (signed-distance-field) texture-atlas and a mapping from
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
  u16     glyphIndex; // sentinel_u16 when character has no glyph (for example a space).
  f32     size;
  f32     offsetX, offsetY;
  f32     advance;
  f32     border; // Size of the sdf border.
} AssetFontTexChar;

ecs_comp_extern_public(AssetFontTexComp) {
  u32               glyphsPerDim;
  f32               lineSpacing;
  f32               baseline;   // How far glyphs can extend below the rectangle.
  AssetFontTexChar* characters; // Sorted on the unicode codepoint.
  usize             characterCount;
};

extern DataMeta g_assetFontTexMeta;

/**
 * Get a character based on a unicode codepoint.
 */
const AssetFontTexChar* asset_fonttex_lookup(const AssetFontTexComp*, Unicode, u8 variation);
