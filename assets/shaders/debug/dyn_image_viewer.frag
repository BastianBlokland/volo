#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "texture.glsl"

struct ImageData {
  u16 imageChannels;
  f16 lod;
  u32 flags;
  f32 exposure;
  f32 aspect;
};

const u32 c_flagsFlipY       = 1 << 0;
const u32 c_flagsAlphaIgnore = 1 << 1;
const u32 c_flagsAlphaOnly   = 1 << 2;

bind_draw_data(0) readonly uniform Draw { ImageData u_draw; };
bind_draw_img(0) uniform sampler2D u_tex;

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
  const u32 imageChannels = u32(u_draw.imageChannels);
  const f32 lod           = f32(u_draw.lod);
  const f32 exposure      = u_draw.exposure;

  const f32v4 imageColor = abs(textureLod(u_tex, coord, lod)) * exposure;
  switch (imageChannels) {
  case 1:
    out_color = imageColor.rrr;
    break;
  case 2:
    out_color = f32v3(imageColor.rg, 0);
    break;
  case 3:
    out_color = imageColor.rgb;
    break;
  case 4:
    if ((u_draw.flags & c_flagsAlphaOnly) != 0) {
      out_color = f32v3(imageColor.a, imageColor.a, imageColor.a);
    } else if ((u_draw.flags & c_flagsAlphaIgnore) != 0) {
      out_color = imageColor.rgb;
    } else {
      out_color = mix(checker_pattern(in_texcoord), imageColor.rgb, imageColor.a);
    }
    break;
  }
}
