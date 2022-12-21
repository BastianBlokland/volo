#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "texture.glsl"

struct ShadeDebugData {
  u32 mode;
};

const u32 c_modeColor     = 1;
const u32 c_modeRoughness = 2;
const u32 c_modeNormal    = 3;
const u32 c_modeDepth     = 4;

bind_global(1) uniform sampler2D u_texGeoColorRough;
bind_global(2) uniform sampler2D u_texGeoNormal;
bind_global(3) uniform sampler2D u_texGeoDepth;
bind_instance_data(0) readonly uniform Instance { ShadeDebugData u_instance; };

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v4 out_color;

void main() {
  const f32v4 colorRough = texture(u_texGeoColorRough, in_texcoord);
  const f32v3 normal     = texture(u_texGeoNormal, in_texcoord).rgb;
  const f32   depth      = texture(u_texGeoDepth, in_texcoord).r;

  const f32v3 color     = colorRough.rgb;
  const f32   roughness = colorRough.a;

  switch (u_instance.mode) {
  case c_modeColor:
    out_color = f32v4(color, 0);
    break;
  case c_modeRoughness:
    out_color = f32v4(roughness, roughness, roughness, 0.0);
    break;
  case c_modeNormal:
    out_color = f32v4(normal, 0);
    break;
  case c_modeDepth:
    out_color = f32v4(depth, depth, depth, 0);
    break;
  default:
    discard;
  }
}
