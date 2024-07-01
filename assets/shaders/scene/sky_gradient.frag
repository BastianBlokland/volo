#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"

/**
 * 3 color gradient based on the y component of the world direction.
 */
const f32v3 c_colorTop    = f32v3(0.4, 0.5, 0.8);
const f32v3 c_colorMiddle = f32v3(1.0, 0.9, 0.9);
const f32v3 c_colorBottom = f32v3(1.0, 0.85, 0.55);
const f32   c_bias        = 0.0001;

bind_internal(0) in f32v3 in_worldViewDir; // NOTE: non-normalized

bind_internal(0) out f32v3 out_color;

void main() {
  const f32 dirY        = normalize(in_worldViewDir).y; /* -1 to 1 */
  const f32 topBlend    = 1.0 - pow(min(1.0, 1.0 + c_bias - dirY /* 2 to 0 */), 4.0);
  const f32 bottomBlend = 1.0 - pow(min(1.0, 1.0 + c_bias + dirY /* 0 to 2 */), 40.0);
  const f32 middleBlend = 1.0 - topBlend - bottomBlend; /* remaining 'blend' */

  out_color = c_colorTop * topBlend + c_colorMiddle * middleBlend + c_colorBottom * bottomBlend;
}
