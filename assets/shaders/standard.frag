#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "light.glsl"
#include "quat.glsl"
#include "texture.glsl"

bind_spec(0) const bool s_shade     = true;
bind_spec(1) const bool s_normalMap = false;

bind_global_data(0) readonly uniform Global { GlobalData u_global; };

bind_graphic(1) uniform sampler2D u_texDiffuse;
bind_graphic(2) uniform sampler2D u_texNormal;

bind_internal(0) in f32v3 in_normal;  // NOTE: non-normalized
bind_internal(1) in f32v4 in_tangent; // NOTE: non-normalized
bind_internal(2) in f32v2 in_texcoord;

bind_internal(0) out f32v4 out_color;

f32v3 surface_normal() {
  if (s_normalMap) {
    return texture_sample_normal(u_texNormal, in_texcoord, in_normal, in_tangent);
  }
  return normalize(in_normal);
}

void main() {
  const f32v4   diffuse = texture_sample_srgb(u_texDiffuse, in_texcoord);
  const f32v3   normal  = surface_normal();
  const f32v3   viewDir = quat_rotate(u_global.camRotation, f32v3(0, 0, 1));
  const Shading shading = s_shade ? light_shade_blingphong(normal, viewDir) : light_shade_flat();

  out_color = light_color(shading, diffuse);
}
