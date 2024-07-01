#include "binding.glsl"
#include "color.glsl"
#include "texture.glsl"

bind_spec(0) const bool s_debug = false;

const u32 c_atomTypeGlyph = 0;
const u32 c_atomTypeImage = 1;

const f32   c_glyphSmoothingPixels = 2;
const f32v4 c_glyphOutlineColor    = f32v4(0.025, 0.025, 0.025, 0.95);
const f32   c_glyphOutlineNormMax  = 0.9; // Avoid extremities of the sdf border to avoid artifacts.
const f32   c_glyphOutlineMin      = 0.001; // Outlines smaller then this will not be drawn.

bind_graphic_img(0) uniform sampler2D u_atlasFont;
bind_graphic_img(1) uniform sampler2D u_atlasImage;

// Generic inputs (used for all atoms).
bind_internal(0) in f32v2 in_uiPos;             // Coordinates in ui-pixels.
bind_internal(1) in f32v2 in_texCoord;          // Texture coordinates of this atom.
bind_internal(2) in flat u32 in_atomType;       // Type of this atom.
bind_internal(3) in flat f32 in_invCanvasScale; // Inverse of the canvas scale.
bind_internal(4) in flat f32v4 in_clipRect;     // Clipping rectangle in ui-pixel coordinates.
bind_internal(5) in flat f32v3 in_texMeta;      // xy: texture origin in atlas, z: texture scale.
bind_internal(6) in flat f32v4 in_color;
bind_internal(7) in flat f32 in_aspectRatio; // Aspect ratio of the atom.
bind_internal(8) in flat f32 in_cornerFrac;  // Corner size in fractions of the atom width.

// Glyph-only inputs.
bind_internal(9) in flat f32 in_glyphInvBorder;      // 1.0 / glyphBorderPixelSize.
bind_internal(10) in flat f32 in_glyphOutlineWidth;  // Desired outline size in ui-pixels.
bind_internal(11) in flat f32 in_glyphEdgeShiftFrac; // Pushes the edge in/out. in frac of width.

bind_internal(0) out f32v4 out_color;

/**
 * Remap a single texture coordinate axis.
 * From: | 0  corner     corner 1.0 |
 * To:   | 0  0.5        0.5    1.0 |
 */
f32 remap_texcoord_axis(const f32 coord, const f32 corner) {
  const f32 leftEdge  = corner;
  const f32 rightEdge = 1 - corner;
  if (coord < leftEdge) {
    return coord / leftEdge * 0.5;
  }
  if (coord > rightEdge) {
    return 0.5 + (coord - rightEdge) / corner * 0.5;
  }
  return 0.5;
}

/**
 * Remap the given texture coordinate to preserve a consistent (unstretched) corner size.
 * Sometimes known as '9 slice scaling'.
 */
f32v2 remap_texcoord(const f32v2 texcoord, const f32 xCorner, const f32 aspectRatio) {
  const f32 x = remap_texcoord_axis(texcoord.x, xCorner);
  const f32 y = remap_texcoord_axis(texcoord.y, xCorner * aspectRatio);
  return f32v2(x, y);
}

/**
 * Compute the final texture coordinates in the atlas.
 */
f32v2 atlas_coord() {
  const f32v2 texOrigin = in_texMeta.xy;
  const f32   texScale  = in_texMeta.z;
  return texOrigin + remap_texcoord(in_texCoord, in_cornerFrac, in_aspectRatio) * texScale;
}

/**
 * Should the given point be clipped.
 */
bool clip(const f32v2 point) {
  return point.x < in_clipRect.x || point.x > in_clipRect.x + in_clipRect.z ||
         point.y < in_clipRect.y || point.y > in_clipRect.y + in_clipRect.w;
}

/**
 * Fade out the glyph beyond the outline edge.
 * 0 = beyond the outline and smoothing ui-pixels.
 * 1 = Precisely on the outer edge of the outline.
 */
f32 glyph_alpha(const f32 distNorm, const f32 outlineNorm, const f32 smoothingNorm) {
  const f32 halfSmoothing = smoothingNorm * 0.5;
  return 1.0 - smoothstep(outlineNorm - halfSmoothing, outlineNorm + halfSmoothing, distNorm);
}

/**
 * Get the fraction between the glyph color and the outline color.
 * 0 = fully glyph color
 * 1 = fully outline color
 */
f32 glyph_outline_frac(const f32 distNorm, const f32 outlineNorm, const f32 smoothingNorm) {
  if (outlineNorm < c_glyphOutlineMin) {
    return 0.0; // Outline is disabled.
  }
  return smoothstep(-smoothingNorm * 0.5, smoothingNorm * 0.5, distNorm);
}

/**
 * Get the signed distance to the glyph edge:
 * -1.0 = Well into the glyph.
 *  0.0 = Precisely on the border of the glyph.
 * +1.0 = Well outside the glyph.
 */
f32 glyph_signed_dist(const f32v2 coord) { return texture(u_atlasFont, coord).r * 2.0 - 1.0; }

f32v4 color_glyph() {
  const f32 invBorder     = in_glyphInvBorder;
  const f32 smoothingNorm = min(c_glyphSmoothingPixels * in_invCanvasScale * invBorder, 1.0);
  const f32 outlineNorm   = in_glyphOutlineWidth * invBorder;

  /**
   * When the outlineNorm is bigger then 0.5 it means there is not enough space in the border for
   * the whole glyph + the outline. In that case we shift the mid-point of the glyph (making it
   * thinner) to give the outline more space. Reasoning behind this is that inconsistencies in
   * outline width are more noticeable then inconsistencies in glyph widths.
   */
  const f32 outlineShift = max(outlineNorm - 0.5, 0);

  const f32v2 atlasCoord  = atlas_coord();
  const f32   distNorm    = glyph_signed_dist(atlasCoord) - in_glyphEdgeShiftFrac + outlineShift;
  const f32   outlineFrac = glyph_outline_frac(distNorm, outlineNorm, smoothingNorm);
  const f32v4 color       = mix(in_color, c_glyphOutlineColor, outlineFrac);
  const f32   alpha       = glyph_alpha(distNorm, outlineNorm, smoothingNorm);

  if (s_debug) {
    return f32v4(outlineFrac * alpha, (distNorm + 1) * 0.5, alpha, 1);
  }
  return f32v4(color.rgb, color.a * alpha);
}

f32v4 color_image() {
  const f32v2 atlasCoord = atlas_coord();
  if (s_debug) {
    return f32v4(atlasCoord.xy, 0, 1);
  }
  // TODO: Support smoothing the edges to reduce aliasing on rotated images.
  const f32v4 imageColor = texture(u_atlasImage, atlasCoord);
  return imageColor * in_color;
}

void main() {
  if (clip(in_uiPos)) {
    discard;
  }

  switch (in_atomType) {
  case c_atomTypeGlyph:
    out_color = color_glyph();
    break;
  case c_atomTypeImage:
    out_color = color_image();
    break;
  }
}
