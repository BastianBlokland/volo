#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "ui.glsl"

const u32 c_maxGlyphs        = 4096;
const u32 c_paletteIndexBits = 2;
const u32 c_paletteSize      = 1 << c_paletteIndexBits;
const u32 c_atlasIndexBits   = 16 - (c_paletteIndexBits + 1); // +1 to skip the sign bit.
const u32 c_atlasIndexMask   = (1 << c_atlasIndexBits) - 1;

const u32   c_verticesPerGlyph                  = 6;
const f32v2 c_unitPositions[c_verticesPerGlyph] = {
    f32v2(0, 1),
    f32v2(1, 1),
    f32v2(0, 0),
    f32v2(1, 1),
    f32v2(1, 0),
    f32v2(0, 0),
};
const f32v2 c_unitTexCoords[c_verticesPerGlyph] = {
    f32v2(0, 1),
    f32v2(1, 1),
    f32v2(0, 0),
    f32v2(1, 1),
    f32v2(1, 0),
    f32v2(0, 0),
};

/**
 * Packed data for 2 glyphs.
 * To fit as much data as possible in the fast uniform storage we fit two glyphs into a single 16
 * byte struct (to meet the 16 byte alignment requirement).
 */
struct GlyphPackedData {
  u16v4 a, b; // x, y = position, z = size, w = 1b unused, 2b palette index, 13b glyphIndex.
};

struct FontData {
  f32   glyphsPerDim;
  f32   invGlyphsPerDim; // 1,0 / glyphsPerDim
  f32v4 palette[c_paletteSize];
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_instance_data(0) readonly uniform Instance {
  FontData        u_font;
  GlyphPackedData u_packedGlyphs[c_maxGlyphs / 2];
};

bind_internal(0) out f32v2 out_texcoord;
bind_internal(1) out flat f32v4 out_color;

/**
 * Retrieve the glyph-data for the given glyph-index.
 */
i32v4 glyph_data(const u32 glyphIndex) {
  /**
   * Find the glyph-tuple (as they are stored in sets of 2) and then retrieve the correct entry.
   */
  const u32  packedIndex = glyphIndex / 2;
  const bool useA        = glyphIndex % 2 == 0;
  return useA ? i32v4(u_packedGlyphs[packedIndex].a) : i32v4(u_packedGlyphs[packedIndex].b);
}

void main() {
  const u32   glyphIndex   = in_vertexIndex / c_verticesPerGlyph;
  const u32   vertIndex    = in_vertexIndex % c_verticesPerGlyph;
  const i32v4 glyphData    = glyph_data(glyphIndex);
  const i32v2 glyphPos     = glyphData.xy;
  const i32   glyphSize    = glyphData.z;
  const u32   paletteIndex = glyphData.w >> c_atlasIndexBits;
  const u32   atlasIndex   = glyphData.w & c_atlasIndexMask;

  /**
   * Compute the ui positions of the vertices.
   */
  const f32v2 uiPos = glyphPos + c_unitPositions[vertIndex] * glyphSize;

  /**
   * Compute the x and y position in the texture atlas based on the glyphIndex.
   */
  const f32v2 atlasPos =
      f32v2(mod(atlasIndex, u_font.glyphsPerDim), floor(atlasIndex * u_font.invGlyphsPerDim));

  out_vertexPosition = ui_norm_to_ndc(uiPos * u_global.resolution.zw);
  out_texcoord       = (c_unitTexCoords[vertIndex] + atlasPos) * u_font.invGlyphsPerDim;
  out_color          = u_font.palette[paletteIndex];
}
