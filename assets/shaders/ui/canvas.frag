#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "color.glsl"
#include "texture.glsl"

const f32 c_smoothingPixels = 2;

bind_graphic(1) uniform sampler2D u_fontTexture;

bind_internal(0) in f32v2 in_texCoord;
bind_internal(1) in flat f32v2 in_texOrigin;
bind_internal(2) in flat f32 in_texScale;
bind_internal(3) in flat f32v4 in_color;
bind_internal(4) in flat f32 in_invBorder;

bind_internal(0) out f32v4 out_color;

/**
 * Fade out the glyph beyond the edge.
 */
f32 get_glyph_alpha(const f32 borderDistNorm, const f32 smoothingNorm) {
  const f32 halfSmoothingNorm = smoothingNorm * 0.5;
  return smoothstep(-halfSmoothingNorm, +halfSmoothingNorm, borderDistNorm);
}

/**
 * Compute the final texture coordinates in the font atlas.
 */
f32v2 get_fontcoord() { return (in_texOrigin + in_texCoord) * in_texScale; }

void main() {
  const f32 smoothingNorm = min(c_smoothingPixels * in_invBorder, 1.0);

  const f32v2 fontCoord      = get_fontcoord();
  const f32   borderDistNorm = (1.0 - texture(u_fontTexture, fontCoord).r * 2.0); // -1 to 1.
  const f32   alpha          = get_glyph_alpha(borderDistNorm, smoothingNorm);

  out_color = f32v4(in_color.rgb, in_color.a * alpha);
}
