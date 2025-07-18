#include "binding.glsl"
#include "global.glsl"
#include "quat.glsl"
#include "texture.glsl"
#include "image_viewer.glsl"

const f32 c_rotateSpeed      = 0.15;
const u32 c_flagsFlipY       = 1 << 0;
const u32 c_flagsAlphaIgnore = 1 << 1;
const u32 c_flagsAlphaOnly   = 1 << 2;

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_draw_data(0) readonly uniform Draw { ImageData u_draw; };
bind_draw_img(0) uniform samplerCube u_tex;

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v4 out_color;

f32v3 checker_pattern(const f32v2 texcoord) {
  const f32v2 scaled = floor(texcoord * 100);
  const f32   val    = max(sign(mod(scaled.x + scaled.y, 2.0)), 0.0);
  return mix(f32v3(0.2), f32v3(0.3), val);
}

f32v3 compute_dir(const f32v2 texcoord, const f32 angle) {
  const f32v3 dir = normalize(f32v3((texcoord - f32v2(0.5)) * 2, 1));
  const f32v4 rot = quat_angle_axis(angle, f32v3(0, 1, 0));
  return quat_rotate(rot, dir);
}

void main() {
  f32v2 coord = in_texcoord;
  if ((u_draw.flags & c_flagsFlipY) != 0) {
    coord.y = 1.0 - coord.y;
  }
  const u32 imageChannels = u_draw.imageChannels;
  const f32 lod           = u_draw.lod;
  const f32 exposure      = u_draw.exposure;

  const f32v3 dir        = compute_dir(coord, u_global.time.y * c_rotateSpeed);
  const f32v4 imageColor = abs(texture_cube_lod(u_tex, dir, lod)) * exposure;
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
      out_color = f32v4(mix(checker_pattern(in_texcoord), imageColor.rgb, imageColor.a), 1);
    }
    break;
  }
}
