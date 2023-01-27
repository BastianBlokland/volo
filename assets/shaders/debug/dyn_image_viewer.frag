#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "texture.glsl"

struct ImageData {
  u32 imageChannels;
  u32 flags;
};

const u32 c_flagsFlipY = 1 << 0;

bind_dynamic(1) uniform sampler2D u_tex;
bind_draw_data(0) readonly uniform Draw { ImageData u_draw; };

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v3 out_color;

f32v3 checker_pattern(const f32v2 texcoord) {
  const f32v2 scaled = floor(texcoord * 100);
  const f32   val    = max(sign(mod(scaled.x + scaled.y, 2.0)), 0.0);
  return mix(f32v3(0.2), f32v3(0.3), val);
}

void main() {
  f32v2 coord = in_texcoord;
  if ((u_draw.flags & c_flagsFlipY) != 0) {
    coord.y = 1.0 - coord.y;
  }

  const f32v4 imageColor = texture(u_tex, coord);
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
    out_color = mix(checker_pattern(in_texcoord), imageColor.rgb, imageColor.a);
    break;
  }
}
