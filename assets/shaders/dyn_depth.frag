#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "instance.glsl"
#include "rand.glsl"

const f32 c_alphaDitherMax = 0.99;

bind_internal(1) in flat f32v4 in_data; // x tag bits, y color, z emissive

void main() {
  const f32v4 color = instance_color(in_data);
  // Dithered transparency.
  if (color.a < c_alphaDitherMax && rand_gradient_noise(in_fragCoord.xy) > color.a) {
    discard;
  }
}
