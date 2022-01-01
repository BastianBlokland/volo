#version 450
#extension GL_GOOGLE_include_directive : enable

#include "include/binding.glsl"

/**
 * 3 color gradient based on the y component of the world direction.
 */
const f32_vec4 c_colorTop    = f32_vec4(0.4, 0.5, 0.8, 1);
const f32_vec4 c_colorMiddle = f32_vec4(0.7, 0.8, 0.9, 1);
const f32_vec4 c_colorBottom = f32_vec4(0.2, 0.2, 0.2, 1);

bind_internal(0) in f32_vec3 in_viewDir; // NOTE: non-normalized

bind_internal(0) out f32_vec4 out_color;

void main() {
  const f32 dirY        = normalize(in_viewDir).y; /* -1 to 1 */
  const f32 topBlend    = 1.0 - pow(min(1.0, 1.0 - dirY /* 2 to 0 */), 4.0);
  const f32 bottomBlend = 1.0 - pow(min(1.0, 1.0 + dirY /* 0 to 2 */), 30.0);
  const f32 middleBlend = 1.0 - topBlend - bottomBlend; /* remaining 'blend' */

  out_color = c_colorTop * topBlend + c_colorMiddle * middleBlend + c_colorBottom * bottomBlend;
}
