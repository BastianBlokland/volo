#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "texture.glsl"

const f32 c_smoothing = 1.0 / 8.0;

bind_graphic(1) uniform sampler2D u_fontTexture;

bind_internal(0) in f32_vec2 in_texcoord;

bind_internal(0) out f32_vec4 out_color;

void main() {
  const f32 dist  = texture_sample_srgb(u_fontTexture, in_texcoord).a;
  const f32 alpha = smoothstep(0.5 - c_smoothing, 0.5 + c_smoothing, dist);
  out_color       = f32_vec4(1, 1, 1, alpha);
}
