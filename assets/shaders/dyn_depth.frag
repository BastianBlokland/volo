#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "rand.glsl"

const f32 c_alphaDitherMax = 0.99;

bind_internal(1) in flat f32v4 in_data; // x tag bits, y alpha, z emissive

void main() {
  const f32 alpha = in_data.y;
  // Dithered transparency.
  if (alpha < c_alphaDitherMax && rand_gradient_noise(in_fragCoord.xy) > alpha) {
    discard;
  }
}
