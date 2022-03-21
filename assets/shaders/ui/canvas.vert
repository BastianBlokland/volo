#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "color.glsl"
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

struct MetaData {
  f32v4 resolution; // x, y size, z, w invSize
  f32   glyphsPerDim;
  f32   invGlyphsPerDim; // 1.0 / glyphsPerDim
  f32v4 clipRects[10];
};

struct GlyphData {
  f32v4 rect; // x, y = position, z, w = size
  u32v4 data; // x = color,
              // y = atlasIndex,
              // z = 16b borderFrac 16b cornerFrac,
              // w = 8b clipId, 8b outlineWidth
};

bind_draw_data(0) readonly uniform Draw { MetaData u_meta; };
bind_instance_data(0) readonly uniform Instance { GlyphData u_glyphs[c_maxInstances]; };

bind_internal(0) out f32v2 out_uiPos;
bind_internal(1) out f32v2 out_texCoord;
bind_internal(2) out flat f32v4 out_clipRect;
bind_internal(3) out flat f32v2 out_texOrigin;
bind_internal(4) out flat f32 out_texScale;
bind_internal(5) out flat f32v4 out_color;
bind_internal(6) out flat f32 out_invBorder;
bind_internal(7) out flat f32 out_outlineWidth;
bind_internal(8) out flat f32 out_aspectRatio;
bind_internal(9) out flat f32 out_cornerFrac;

void main() {
  const GlyphData glyphData    = u_glyphs[in_instanceIndex];
  const f32v2     glyphPos     = glyphData.rect.xy;
  const f32v2     glyphSize    = glyphData.rect.zw;
  const f32v4     glyphColor   = color_from_u32(glyphData.data.x);
  const u32       atlasIndex   = glyphData.data.y;
  const f32       borderFrac   = (glyphData.data.z & 0xFFFF) / f32(0xFFFF);
  const f32       cornerFrac   = (glyphData.data.z >> 16) / f32(0xFFFF);
  const u32       clipId       = glyphData.data.w & 0xFF;
  const u32       outlineWidth = (glyphData.data.w >> 8) & 0xFF;

  /**
   * Compute the ui positions of the vertices.
   */
  const f32v2 uiPos = glyphPos + c_unitPositions[in_vertexIndex] * glyphSize;

  /**
   * Compute the x and y position in the texture atlas based on the glyphIndex.
   */
  const f32v2 texOrigin =
      f32v2(mod(atlasIndex, u_meta.glyphsPerDim), floor(atlasIndex * u_meta.invGlyphsPerDim));

  out_vertexPosition = ui_norm_to_ndc(uiPos * u_meta.resolution.zw);
  out_uiPos          = uiPos;
  out_clipRect       = u_meta.clipRects[clipId];
  out_texCoord       = c_unitTexCoords[in_vertexIndex];
  out_texOrigin      = texOrigin;
  out_texScale       = u_meta.invGlyphsPerDim;
  out_color          = glyphColor;
  out_invBorder      = 1.0 / (glyphSize.x * borderFrac);
  out_outlineWidth   = outlineWidth;
  out_aspectRatio    = glyphSize.x / glyphSize.y;
  out_cornerFrac     = cornerFrac;
}
