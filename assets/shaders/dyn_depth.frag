#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "rand.glsl"

bind_internal(1) in flat f32v4 in_data;

void main() {
  const f32 alpha = in_data.y;
  // Dithered transparency.
  if (rand_gradient_noise(in_fragCoord.xy) > alpha) {
    discard;
  }
}
