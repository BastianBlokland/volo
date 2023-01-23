#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "texture.glsl"

struct ImageData {
  u32 imageChannels;
};

bind_dynamic(1) uniform sampler2D u_tex;
bind_draw_data(0) readonly uniform Draw { ImageData u_draw; };

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v3 out_color;

void main() {
  const f32v4 imageColor = texture(u_tex, in_texcoord);
  switch (u_draw.imageChannels) {
  case 1:
    out_color = imageColor.rrr;
    break;
  case 2:
    out_color = f32v3(imageColor.rg, 1);
    break;
  case 3:
    out_color = imageColor.rgb;
    break;
  case 4:
    // TODO: Visualize alpha, perhaps using a checkerboard pattern.
    out_color = imageColor.rgb;
    break;
  }
}
