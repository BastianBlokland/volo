#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "color.glsl"
#include "texture.glsl"

const f32   c_smoothing    = 4;
const f32   c_outlineWidth = 6;
const f32v4 c_outlineColor = color_black;

bind_graphic(1) uniform sampler2D u_fontTexture;

bind_internal(0) in f32v2 in_texCoord;
bind_internal(1) in flat f32v2 in_texOrigin;
bind_internal(2) in flat f32 in_texScale;
bind_internal(3) in flat f32v4 in_color;
bind_internal(4) in flat f32 in_glyphSize;

bind_internal(0) out f32v4 out_color;

/**
 * Fades out glyph beyond the outline.
 */
f32 get_glyph_alpha(const f32 dist) {
  const f32 alphaStart = -c_outlineWidth - c_smoothing;
  const f32 alphaEnd   = -c_outlineWidth + c_smoothing;
  return smoothstep(alphaStart, alphaEnd, dist);
}

/**
 * Get the fraction between the glyph color and the outline color.
 * 0 = fully outline color, 1 = fully glyph color.
 */
f32 get_color_frac(const f32 dist) { return smoothstep(-c_smoothing, +c_smoothing, dist); }

/**
 * Compute the final texture coordinates in the font atlas.
 */
f32v2 get_fontcoord() { return (in_texOrigin + in_texCoord) * in_texScale; }

void main() {
  const f32v2 fontCoord = get_fontcoord();
  const f32   dist      = (1.0 - texture(u_fontTexture, fontCoord).r * 2.0) * in_glyphSize;
  const f32v3 color     = mix(c_outlineColor.rgb, in_color.rgb, get_color_frac(dist));
  out_color             = f32v4(color, in_color.a * get_glyph_alpha(dist));
}
