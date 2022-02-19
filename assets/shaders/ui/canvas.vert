#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "ui.glsl"

const u32 c_maxGlyphs = 1024;

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

struct CanvasData {
  f32 glyphsPerDim;
  f32 invGlyphsPerDim; // 1,0 / glyphsPerDim
};

struct GlyphData {
  f32v4 rect; // x, y = position, z, w = size
  u32v4 data; // x = atlasIndex
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_instance_data(0) readonly uniform Instance {
  CanvasData u_canvas;
  GlyphData  u_glyphs[c_maxGlyphs];
};

bind_internal(0) out f32v2 out_texcoord;

void main() {
  const u32       glyphIndex = in_vertexIndex / c_verticesPerGlyph;
  const u32       vertIndex  = in_vertexIndex % c_verticesPerGlyph;
  const GlyphData glyphData  = u_glyphs[glyphIndex];
  const f32v2     glyphPos   = glyphData.rect.xy;
  const f32v2     glyphSize  = glyphData.rect.zw;
  const u32       atlasIndex = glyphData.data.x;

  /**
   * Compute the ui positions of the vertices.
   */
  const f32v2 uiPos = glyphPos + c_unitPositions[vertIndex] * glyphSize;

  /**
   * Compute the x and y position in the texture atlas based on the glyphIndex.
   */
  const f32v2 atlasPos =
      f32v2(mod(atlasIndex, u_canvas.glyphsPerDim), floor(atlasIndex * u_canvas.invGlyphsPerDim));

  out_vertexPosition = ui_norm_to_ndc(uiPos * u_global.resolution.zw);
  out_texcoord       = (c_unitTexCoords[vertIndex] + atlasPos) * u_canvas.invGlyphsPerDim;
}
