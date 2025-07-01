#include "binding.glsl"
#include "texture.glsl"
#include "image_viewer.glsl"

const u32 c_flagsFlipY       = 1 << 0;
const u32 c_flagsAlphaIgnore = 1 << 1;
const u32 c_flagsAlphaOnly   = 1 << 2;

bind_draw_data(0) readonly uniform Draw { ImageData u_draw; };
bind_draw_img(0) uniform sampler2DArray u_tex;

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v4 out_color;

f32v3 checker_pattern(const f32v2 texcoord, const f32 aspect) {
  const f32v2 scaled = floor(f32v2(texcoord.x, texcoord.y / aspect) * 100);
  const f32   val    = max(sign(mod(scaled.x + scaled.y, 2.0)), 0.0);
  return mix(f32v3(0.2), f32v3(0.3), val);
}

void main() {
  f32v2 coord = in_texcoord;
  if ((u_draw.flags & c_flagsFlipY) != 0) {
    coord.y = 1.0 - coord.y;
  }
  const u32 imageChannels = u_draw.imageChannels;
  const f32 lod           = u_draw.lod;
  const f32 layer         = u_draw.layer;
  const f32 exposure      = u_draw.exposure;

  const f32v4 imageColor = abs(textureLod(u_tex, f32v3(coord, layer), lod)) * exposure;
  switch (imageChannels) {
  case 1:
    out_color = f32v4(imageColor.rrr, 1);
    break;
  case 2:
    out_color = f32v4(imageColor.rg, 0, 1);
    break;
  case 3:
    out_color = f32v4(imageColor.rgb, 1);
    break;
  case 4:
    if ((u_draw.flags & c_flagsAlphaOnly) != 0) {
      out_color = f32v4(imageColor.a, imageColor.a, imageColor.a, 1);
    } else if ((u_draw.flags & c_flagsAlphaIgnore) != 0) {
      out_color = f32v4(imageColor.rgb, 1);
    } else {
      const f32v3 checker = checker_pattern(in_texcoord, u_draw.aspect);
      out_color           = f32v4(mix(checker, imageColor.rgb, imageColor.a), 1);
    }
    break;
  }
}
