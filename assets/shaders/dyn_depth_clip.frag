#include "binding.glsl"
#include "instance.glsl"
#include "rand.glsl"
#include "texture.glsl"

const f32 c_alphaTextureThreshold = 0.2;
const f32 c_alphaDitherMax        = 0.99;

bind_draw_img(0) uniform sampler2D u_texAlpha;

bind_internal(0) in f32v2 in_texcoord;
bind_internal(1) in flat f32v4 in_data; // x tag bits, y color, z emissive

void main() {
  f32v4 color = instance_color(in_data);
  if (texture(u_texAlpha, in_texcoord).r < c_alphaTextureThreshold) {
    color.a = 0.0;
  }
  // Dithered transparency.
  if (color.a < c_alphaDitherMax && rand_gradient_noise(in_fragCoord.xy) > color.a) {
    discard;
  }
}
