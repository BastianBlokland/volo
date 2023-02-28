#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "rand.glsl"
#include "texture.glsl"

const f32 c_alphaTextureThreshold = 0.2;
const f32 c_alphaDitherMax        = 0.99;

bind_dynamic_img(0) uniform sampler2D u_texAlpha;

bind_internal(0) in f32v2 in_texcoord;
bind_internal(1) in flat f32v4 in_data;

void main() {
  f32 alpha = in_data.y;
  if (texture(u_texAlpha, in_texcoord).r < c_alphaTextureThreshold) {
    alpha = 0.0;
  }
  // Dithered transparency.
  if (alpha < c_alphaDitherMax && rand_gradient_noise(in_fragCoord.xy) > alpha) {
    discard;
  }
}
