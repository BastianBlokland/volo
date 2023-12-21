#version 450
#extension GL_GOOGLE_include_directive : enable

#include "atlas.glsl"
#include "binding.glsl"
#include "color.glsl"
#include "instance.glsl"
#include "math.glsl"
#include "ui.glsl"

const u32   c_vertexCount                  = 6;
const f32v2 c_unitPositions[c_vertexCount] = {
    f32v2(-0.5, +0.5),
    f32v2(+0.5, +0.5),
    f32v2(-0.5, -0.5),
    f32v2(+0.5, +0.5),
    f32v2(+0.5, -0.5),
    f32v2(-0.5, -0.5),
};
const f32v2 c_unitTexCoords[c_vertexCount] = {
    f32v2(0, 1),
    f32v2(1, 1),
    f32v2(0, 0),
    f32v2(1, 1),
    f32v2(1, 0),
    f32v2(0, 0),
};
const u32 c_maxClipRects  = 50;
const u32 c_atomTypeGlyph = 0;

struct MetaData {
  f32v4     canvasData; // x + y = inverse canvas size in ui-pixels, z = inverse canvas-scale.
  AtlasMeta atlasFont;  // Font atlas meta-data.
  f32v4     clipRects[c_maxClipRects];
};

struct AtomData {
  f32v4 rect; // x + y = position, z + w = size
  u32v4 data; // x = color,
              // y = 16b atlasIndex, 16b angleFrac,
              // z = 16b borderFrac 16b cornerFrac,
              // w = 8b atomType, 8b clipId, 8b outlineWidth, 8b weight
};

bind_draw_data(0) readonly uniform Draw { MetaData u_meta; };
bind_instance_data(0) readonly uniform Instance { AtomData u_atoms[c_maxInstances]; };

bind_internal(0) out f32v2 out_uiPos;
bind_internal(1) out f32v2 out_texCoord;
bind_internal(2) out flat u32 out_atomType;
bind_internal(3) out flat f32 out_invCanvasScale;
bind_internal(4) out flat f32v4 out_clipRect;
bind_internal(5) out flat f32v3 out_texMeta; // xy: origin, z: scale.
bind_internal(6) out flat f32v4 out_color;
bind_internal(7) out flat f32 out_invBorder;
bind_internal(8) out flat f32 out_outlineWidth;
bind_internal(9) out flat f32 out_aspectRatio;
bind_internal(10) out flat f32 out_cornerFrac;
bind_internal(11) out flat f32 out_edgeShiftFrac;

/**
 * Compute the shape edge shift in fractions of the glyphs width.
 */
f32 glyph_edge_shift(const u32 weight) {
  /**
   * Possible weight values:
   *   0: Light
   *   1: Normal
   *   2: Bold
   *   3: Heavy
   */
  return (weight - 1.0) * 0.05;
}

void main() {
  const AtomData atomData     = u_atoms[in_instanceIndex];
  const f32v2    atomPos      = atomData.rect.xy;
  const f32v2    atomSize     = atomData.rect.zw;
  const f32v4    atomColor    = color_from_u32(atomData.data.x);
  const u32      atlasIndex   = atomData.data.y & 0xFFFF;
  const f32      angleRad     = (atomData.data.y >> 16) / f32(0xFFFF) * c_pi * 2;
  const f32      borderFrac   = (atomData.data.z & 0xFFFF) / f32(0xFFFF);
  const f32      cornerFrac   = (atomData.data.z >> 16) / f32(0xFFFF);
  const u32      atomType     = (atomData.data.w >> 0) & 0xFF;
  const u32      clipId       = (atomData.data.w >> 8) & 0xFF;
  const u32      outlineWidth = (atomData.data.w >> 16) & 0xFF;
  const u32      weight       = (atomData.data.w >> 24) & 0xFF;

  const f32m2 rotMat = math_rotate_mat_f32m2(angleRad);

  /**
   * Compute the ui positions of the vertices.
   * NOTE: Expected origin of the atom is in the lower left hand corner but rotation should happen
   * around the center of the atom.
   */
  const f32v2 uiPosRel = rotMat * (c_unitPositions[in_vertexIndex] * atomSize) + atomSize * 0.5;
  const f32v2 uiPos    = atomPos + uiPosRel;

  const f32v2 texOrigin      = atlas_entry_origin(u_meta.atlasFont, atlasIndex);
  const f32v2 invCanvasSize  = u_meta.canvasData.xy;
  const f32   invCanvasScale = u_meta.canvasData.z;

  out_vertexPosition = ui_norm_to_ndc(uiPos * invCanvasSize);
  out_uiPos          = uiPos;
  out_texCoord       = c_unitTexCoords[in_vertexIndex];
  out_atomType       = atomType;
  out_invCanvasScale = invCanvasScale;
  out_clipRect       = u_meta.clipRects[clipId];
  out_texMeta        = f32v3(texOrigin, atlas_entry_size(u_meta.atlasFont));
  out_color          = atomColor;
  out_invBorder      = 1.0 / (atomSize.x * borderFrac);
  out_outlineWidth   = outlineWidth;
  out_aspectRatio    = atomSize.x / atomSize.y;
  out_cornerFrac     = cornerFrac;
  out_edgeShiftFrac  = glyph_edge_shift(weight);
}
