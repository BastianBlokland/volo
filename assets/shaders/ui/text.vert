#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "ui.glsl"

const u32      c_verticesPerGlyph                  = 6;
const f32_vec2 c_unitPositions[c_verticesPerGlyph] = {
    f32_vec2(0, 1),
    f32_vec2(1, 1),
    f32_vec2(0, 0),
    f32_vec2(1, 1),
    f32_vec2(1, 0),
    f32_vec2(0, 0),
};
const f32_vec2 c_unitTexCoords[c_verticesPerGlyph] = {
    f32_vec2(0, 1),
    f32_vec2(1, 1),
    f32_vec2(0, 0),
    f32_vec2(1, 1),
    f32_vec2(1, 0),
    f32_vec2(0, 0),
};

bind_spec(0) const u32 s_maxGlyphs = 2048;

struct GlyphData {
  f32_vec4 raw; // x, y = position, z = size, w = glyphIndex.
};

struct FontData {
  f32      glyphsPerDim;
  f32      invGlyphsPerDim;
  f32_vec4 color;
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_instance_data(0) readonly uniform Instance {
  FontData  u_font;
  GlyphData u_glyphs[s_maxGlyphs];
};

bind_internal(0) out f32_vec2 out_texcoord;
bind_internal(1) out flat f32_vec4 out_color;

void main() {
  const u32      glyphIndex = in_vertexIndex / c_verticesPerGlyph;
  const u32      vertIndex  = in_vertexIndex % c_verticesPerGlyph;
  const f32_vec2 glyphPos   = u_glyphs[glyphIndex].raw.xy;
  const f32      glyphSize  = u_glyphs[glyphIndex].raw.z;
  const f32      atlasIndex = u_glyphs[glyphIndex].raw.w;

  /**
   * Compute the ui positions of the vertices.
   */
  const f32_vec2 uiPos = glyphPos + c_unitPositions[vertIndex] * glyphSize;

  /**
   * Compute the x and y position in the texture atlas based on the glyphIndex.
   */
  const f32_vec2 atlasPos =
      f32_vec2(mod(atlasIndex, u_font.glyphsPerDim), floor(atlasIndex * u_font.invGlyphsPerDim));

  out_vertexPosition = ui_norm_to_ndc(uiPos * u_global.resolution.zw);
  out_texcoord       = (c_unitTexCoords[vertIndex] + atlasPos) * u_font.invGlyphsPerDim;
  out_color          = u_font.color;
}
