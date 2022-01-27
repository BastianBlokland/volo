#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "ui.glsl"

const u32 c_maxGlyphs        = 2048;
const u32 c_paletteIndexBits = 2;
const u32 c_paletteSize      = 1 << c_paletteIndexBits;
const u32 c_atlasIndexBits   = 32 - c_paletteIndexBits;
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

struct FontData {
  f32   glyphsPerDim;
  f32   invGlyphsPerDim; // 1,0 / glyphsPerDim
  f32v4 palette[c_paletteSize];
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_instance_data(0) readonly uniform Instance {
  FontData u_font;
  f32v4 u_glyphs[c_maxGlyphs]; // x, y = position, z = size, w = 2b palette index, 30b glyphIndex.
};

bind_internal(0) out f32v2 out_texcoord;
bind_internal(1) out flat f32v4 out_color;

void main() {
  const u32   glyphIndex   = in_vertexIndex / c_verticesPerGlyph;
  const u32   vertIndex    = in_vertexIndex % c_verticesPerGlyph;
  const f32v4 glyphData    = u_glyphs[glyphIndex];
  const f32v2 glyphPos     = glyphData.xy;
  const f32   glyphSize    = glyphData.z;
  const u32   paletteIndex = floatBitsToUint(glyphData.w) >> c_atlasIndexBits;
  const u32   atlasIndex   = floatBitsToUint(glyphData.w) & c_atlasIndexMask;

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
