#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "color.glsl"
#include "global.glsl"
#include "instance.glsl"
#include "ui.glsl"

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

struct DrawData {
  f32 glyphsPerDim;
  f32 invGlyphsPerDim; // 1.0 / glyphsPerDim
};

struct GlyphData {
  f32v4 rect; // x, y = position, z, w = size
  u32v4 data; // x = color, y = atlasIndex, z = 16b borderFrac 16b cornerFrac, w = outlineWidth
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_draw_data(0) readonly uniform Draw { DrawData u_draw; };
bind_instance_data(0) readonly uniform Instance { GlyphData u_glyphs[c_maxInstances]; };

bind_internal(0) out f32v2 out_texCoord;
bind_internal(1) out flat f32v2 out_texOrigin;
bind_internal(2) out flat f32 out_texScale;
bind_internal(3) out flat f32v4 out_color;
bind_internal(4) out flat f32 out_invBorder;
bind_internal(5) out flat f32 out_outlineWidth;
bind_internal(6) out flat f32 out_aspectRatio;
bind_internal(7) out flat f32 out_cornerFrac;

void main() {
  const GlyphData glyphData    = u_glyphs[in_instanceIndex];
  const f32v2     glyphPos     = glyphData.rect.xy;
  const f32v2     glyphSize    = glyphData.rect.zw;
  const f32v4     glyphColor   = color_from_u32(glyphData.data.x);
  const u32       atlasIndex   = glyphData.data.y;
  const f32       borderFrac   = (glyphData.data.z & 0xFFFF) / f32(0xFFFF);
  const f32       cornerFrac   = (glyphData.data.z >> 16) / f32(0xFFFF);
  const u32       outlineWidth = glyphData.data.w;

  /**
   * Compute the ui positions of the vertices.
   */
  const f32v2 uiPos = glyphPos + c_unitPositions[in_vertexIndex] * glyphSize;

  /**
   * Compute the x and y position in the texture atlas based on the glyphIndex.
   */
  const f32v2 texOrigin =
      f32v2(mod(atlasIndex, u_draw.glyphsPerDim), floor(atlasIndex * u_draw.invGlyphsPerDim));

  out_vertexPosition = ui_norm_to_ndc(uiPos * u_global.resolution.zw);
  out_texCoord       = c_unitTexCoords[in_vertexIndex];
  out_texOrigin      = texOrigin;
  out_texScale       = u_draw.invGlyphsPerDim;
  out_color          = glyphColor;
  out_invBorder      = 1.0 / (glyphSize.x * borderFrac);
  out_outlineWidth   = outlineWidth;
  out_aspectRatio    = glyphSize.x / glyphSize.y;
  out_cornerFrac     = cornerFrac;
}
