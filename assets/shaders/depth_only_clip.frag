#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "texture.glsl"

const f32 c_alphaClipThreshold = 0.2;

bind_graphic(3) uniform sampler2D u_texAlpha;

bind_internal(2) in f32v2 in_texcoord;

void main() {
  // Discard fragment if the alpha is below the threshold.
  const f32 alpha = texture(u_texAlpha, in_texcoord).r;
  if (alpha < c_alphaClipThreshold) {
    discard;
  }
}
