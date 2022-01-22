#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "ui.glsl"

const u32      c_verticesPerChar                  = 6;
const f32_vec2 c_unitPositions[c_verticesPerChar] = {
    f32_vec2(0, 1),
    f32_vec2(1, 1),
    f32_vec2(0, 0),
    f32_vec2(1, 1),
    f32_vec2(1, 0),
    f32_vec2(0, 0),
};
const f32_vec2 c_unitTexCoords[c_verticesPerChar] = {
    f32_vec2(0, 1),
    f32_vec2(1, 1),
    f32_vec2(0, 0),
    f32_vec2(1, 1),
    f32_vec2(1, 0),
    f32_vec2(0, 0),
};

bind_spec(0) const u32 s_maxChars = 2048;

struct CharData {
  f32_vec4 raw; // x, y = position, z = size, w = glyphIndex.
};

struct FontData {
  f32 glyphsPerDim;
  f32 invGlyphsPerDim;
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_instance_data(0) readonly uniform Instance {
  FontData u_font;
  CharData u_chars[s_maxChars];
};

bind_internal(0) out f32_vec2 out_texcoord;

void main() {
  const u32      charIndex  = in_vertexIndex / c_verticesPerChar;
  const f32_vec2 charPos    = u_chars[charIndex].raw.xy;
  const f32      charSize   = u_chars[charIndex].raw.z;
  const f32      glyphIndex = u_chars[charIndex].raw.w;

  /**
   * Compute the ui positions of the vertices.
   */
  const f32_vec2 uiPos = charPos + c_unitPositions[in_vertexIndex] * charSize;

  /**
   * Compute the x and y position in the texture atlas based on the glyphIndex.
   */
  const f32_vec2 atlasPos =
      f32_vec2(mod(glyphIndex, u_font.glyphsPerDim), floor(glyphIndex * u_font.invGlyphsPerDim));

  out_vertexPosition = ui_norm_to_ndc(uiPos * u_global.resolution.zw);
  out_texcoord       = (c_unitTexCoords[in_vertexIndex] + atlasPos) * u_font.invGlyphsPerDim;
}
