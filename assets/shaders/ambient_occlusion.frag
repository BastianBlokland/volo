#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "texture.glsl"

const u32 c_kernelSize = 16; // Needs to match the maximum in rend_painter.c

struct AoData {
  f32v4 kernel[c_kernelSize];
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_global(1) uniform sampler2D u_texGeoNormalTags;
bind_global(2) uniform sampler2D u_texGeoDepth;
bind_draw_data(0) readonly uniform Draw { AoData u_draw; };

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32 out_occlusion;

void main() {
  const f32v4 normalTags = texture(u_texGeoNormalTags, in_texcoord);
  const f32v3 normal     = normal_tex_decode(normalTags.xyz);
  const f32   depth      = texture(u_texGeoDepth, in_texcoord).r;

  out_occlusion = depth + normal.x;
}
