#include "binding.glsl"
#include "global.glsl"
#include "rand.glsl"
#include "texture.glsl"

const f32 c_alphaDitherMax = 0.99;
const f32 c_alphaMult      = 0.5;

bind_global_data(0) readonly uniform Global { GlobalData u_global; };

bind_graphic_img(0) uniform sampler2D u_atlas;

bind_internal(0) in flat f32v4 in_color;
bind_internal(1) in flat f32 in_opacity;
bind_internal(3) in f32v2 in_texcoord;

void main() {
  const f32v4 texSample = texture(u_atlas, in_texcoord);
  const f32   alpha     = texSample.a * in_color.a * in_opacity * c_alphaMult;

  // Dithered transparency.
  if (alpha < c_alphaDitherMax && rand_gradient_noise(in_fragCoord.xy) > alpha) {
    discard;
  }
}
