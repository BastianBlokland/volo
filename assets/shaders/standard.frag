#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "light.glsl"
#include "quat.glsl"
#include "texture.glsl"

bind_spec(0) const bool s_shade         = true;
bind_spec(1) const bool s_normalMap     = false;
bind_spec(2) const bool s_reflectSkybox = false;
bind_spec(3) const f32 s_reflectFrac    = 0.5;

bind_global_data(0) readonly uniform Global { GlobalData u_global; };

bind_graphic(1) uniform sampler2D u_texDiffuse;
bind_graphic(2) uniform sampler2D u_texNormal;
bind_graphic(3) uniform samplerCube u_cubeSkybox;

bind_internal(0) in f32v3 in_position;
bind_internal(1) in f32v3 in_normal;  // NOTE: non-normalized
bind_internal(2) in f32v4 in_tangent; // NOTE: non-normalized
bind_internal(3) in f32v2 in_texcoord;

bind_internal(0) out f32v4 out_color;

f32v3 surface_normal() {
  if (s_normalMap) {
    return texture_sample_normal(u_texNormal, in_texcoord, in_normal, in_tangent);
  }
  return normalize(in_normal);
}

f32v4 compute_reflection(const f32v4 diffuse, const f32v3 normal, const f32v3 viewDir) {
  if (s_reflectSkybox) {
    const f32v3 dir = reflect(viewDir, normal);
    return mix(diffuse, texture_cube_srgb(u_cubeSkybox, dir), s_reflectFrac);
  }
  return diffuse;
}

void main() {
  const f32v4   diffuse = texture_sample_srgb(u_texDiffuse, in_texcoord);
  const f32v3   normal  = surface_normal();
  const f32v3   viewDir = normalize(in_position - u_global.camPosition.xyz);
  const Shading shading = s_shade ? light_shade_blingphong(normal, viewDir) : light_shade_flat();

  const f32v4 diffuseWithRefl = compute_reflection(diffuse, normal, viewDir);
  out_color                   = light_color(shading, diffuseWithRefl);
}
