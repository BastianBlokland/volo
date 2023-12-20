#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "color.glsl"
#include "texture.glsl"

bind_spec(0) const bool s_debug = false;

const f32   c_smoothingPixels = 2;
const f32v4 c_outlineColor    = f32v4(0.025, 0.025, 0.025, 0.95);
const f32   c_outlineNormMax  = 0.9; // Avoid the extremities of the sdf border to avoid artifacts.
const f32   c_outlineMin      = 0.001; // Outlines smaller then this will not be drawn.

bind_graphic_img(0) uniform sampler2D u_fontTexture;

bind_internal(0) in f32v2 in_uiPos;             // Coordinates in ui-pixels.
bind_internal(1) in f32v2 in_texCoord;          // Texture coordinates of this glyph.
bind_internal(2) in flat f32 in_invCanvasScale; // Inverse of the canvas scale.
bind_internal(3) in flat f32v4 in_clipRect;     // Clipping rectangle in ui-pixel coordinates.
bind_internal(4) in flat f32v2 in_texOrigin;    // Origin of the glyph in the font atlas.
bind_internal(5) in flat f32 in_texScale;       // Scale of the glyph in the font atlas.
bind_internal(6) in flat f32v4 in_color;
bind_internal(7) in flat f32 in_invBorder;      // 1.0 / borderPixelSize
bind_internal(8) in flat f32 in_outlineWidth;   // Desired outline size in ui-pixels.
bind_internal(9) in flat f32 in_aspectRatio;    // Aspect ratio of the glyph
bind_internal(10) in flat f32 in_cornerFrac;    // Corner size in fractions of the glyph width.
bind_internal(11) in flat f32 in_edgeShiftFrac; // Pushes the edge in or out, in fractions of width.

bind_internal(0) out f32v4 out_color;

/**
 * Fade out the glyph beyond the outline edge.
 * 0 = beyond the outline and smoothing ui-pixels.
 * 1 = Precisely on the outer edge of the outline.
 */
f32 get_glyph_alpha(const f32 distNorm, const f32 outlineNorm, const f32 smoothingNorm) {
  const f32 halfSmoothing = smoothingNorm * 0.5;
  return 1.0 - smoothstep(outlineNorm - halfSmoothing, outlineNorm + halfSmoothing, distNorm);
}

/**
 * Get the fraction between the glyph color and the outline color.
 * 0 = fully glyph color
 * 1 = fully outline color
 */
f32 get_outline_frac(const f32 distNorm, const f32 outlineNorm, const f32 smoothingNorm) {
  if (outlineNorm < c_outlineMin) {
    return 0.0; // Outline is disabled.
  }
  return smoothstep(-smoothingNorm * 0.5, smoothingNorm * 0.5, distNorm);
}

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
 * Compute the final texture coordinates in the font atlas.
 */
f32v2 get_fontcoord() {
  return in_texOrigin + remap_texcoord(in_texCoord, in_cornerFrac, in_aspectRatio) * in_texScale;
}

/**
 * Get the signed distance to the glyph edge:
 * -1.0 = Well into the glyph.
 *  0.0 = Precisely on the border of the glyph.
 * +1.0 = Well outside the glyph.
 */
f32 get_signed_dist_to_glyph(const f32v2 coord) {
  return texture(u_fontTexture, coord).r * 2.0 - 1.0;
}

/**
 * Should the given point be clipped.
 */
bool clip(const f32v2 point) {
  return point.x < in_clipRect.x || point.x > in_clipRect.x + in_clipRect.z ||
         point.y < in_clipRect.y || point.y > in_clipRect.y + in_clipRect.w;
}

void main() {
  if (clip(in_uiPos)) {
    discard;
  }

  const f32 smoothingNorm = min(c_smoothingPixels * in_invCanvasScale * in_invBorder, 1.0);
  const f32 outlineNorm   = in_outlineWidth * in_invBorder;

  /**
   * When the outlineNorm is bigger then 0.5 it means there is not enough space in the border for
   * the whole glyph + the outline. In that case we shift the mid-point of the glyph (making it
   * thinner) to give the outline more space. Reasoning behind this is that inconsistencies in
   * outline width are more noticeable then inconsistencies in glyph widths.
   */
  const f32 outlineShift = max(outlineNorm - 0.5, 0);

  const f32v2 fontCoord   = get_fontcoord();
  const f32   distNorm    = get_signed_dist_to_glyph(fontCoord) - in_edgeShiftFrac + outlineShift;
  const f32   outlineFrac = get_outline_frac(distNorm, outlineNorm, smoothingNorm);
  const f32v4 color       = mix(in_color, c_outlineColor, outlineFrac);
  const f32   alpha       = get_glyph_alpha(distNorm, outlineNorm, smoothingNorm);

  if (s_debug) {
    out_color = f32v4(outlineFrac * alpha, (distNorm + 1) * 0.5, alpha, 1);
  } else {
    out_color = f32v4(color.rgb, color.a * alpha);
  }
}
