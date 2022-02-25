#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "color.glsl"
#include "texture.glsl"

const f32   c_smoothingPixels = 2;
const f32v4 c_outlineColor    = color_black;
const f32   c_outlineNormMax  = 0.9;  // Avoid the extremities of the sdf border to avoid artifacts.
const f32   c_outlineMin      = 0.01; // Outlines smaller then this will not be drawn.

bind_graphic(1) uniform sampler2D u_fontTexture;

bind_internal(0) in f32v2 in_texCoord;       // Texture coordinates of this glyph.
bind_internal(1) in flat f32v2 in_texOrigin; // Origin of the glyph in the font atlas.
bind_internal(2) in flat f32 in_texScale;    // Scale of the glyph in the font atlas.
bind_internal(3) in flat f32v4 in_color;
bind_internal(4) in flat f32 in_invBorder;    // 1.0 / borderPixelSize
bind_internal(5) in flat f32 in_outlineWidth; // Desired outline size in pixels.
bind_internal(6) in flat f32 in_aspectRatio;  // Aspect ratio of the glyph
bind_internal(7) in flat f32 in_cornerFrac;   // Corner size in fractions of the glyph width.

bind_internal(0) out f32v4 out_color;

/**
 * Fade out the glyph beyond the outline edge.
 * 0 = beyond the outline and smoothing pixels.
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
  return smoothstep(-smoothingNorm, 0, distNorm);
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
  return (in_texOrigin + remap_texcoord(in_texCoord, in_cornerFrac, in_aspectRatio)) * in_texScale;
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

void main() {
  const f32 smoothingNorm = min(c_smoothingPixels * in_invBorder, 1.0);
  const f32 outlineNorm   = min(in_outlineWidth * in_invBorder, c_outlineNormMax - smoothingNorm);

  const f32   distNorm    = get_signed_dist_to_glyph(get_fontcoord());
  const f32   outlineFrac = get_outline_frac(distNorm, outlineNorm, smoothingNorm);
  const f32v3 color       = mix(in_color.rgb, c_outlineColor.rgb, outlineFrac);
  const f32   alpha       = get_glyph_alpha(distNorm, outlineNorm, smoothingNorm);

  out_color = f32v4(color, in_color.a * alpha);
}
