#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "quat.glsl"
#include "texture.glsl"

const f32v3 c_lightDir       = normalize(f32v3(0.4, 0.6, -1.0));
const f32v4 c_lightColor     = f32v4(1.0, 0.9, 0.7, 1.0);
const f32   c_lightIntensity = 0.8;
const f32   c_lightShininess = 16;
const f32   c_lightAmbient   = 0.02;

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

struct Shading {
  f32 lambertian;
  f32 ambient;
  f32 specular;
};

Shading shade_flat() {
  Shading res;
  res.lambertian = 0.0;
  res.ambient    = 1.0;
  res.specular   = 0.0;
  return res;
}

Shading shade_blingphong(const f32v3 normal, const f32v3 viewDir) {
  Shading res;
  res.lambertian = max(dot(normal, c_lightDir), 0.0) * c_lightIntensity;
  res.ambient    = c_lightAmbient;
  res.specular   = 0.0;
  if (res.lambertian > 0.0) {
    const f32v3 halfDir   = normalize(c_lightDir - viewDir);
    const f32   specAngle = max(dot(normal, halfDir), 0.0);
    res.specular          = pow(specAngle, c_lightShininess) * c_lightIntensity;
  }
  return res;
}

void main() {
  const f32v4   diffuse = texture_sample_srgb(u_texDiffuse, in_texcoord);
  const f32v3   normal  = surface_normal();
  const f32v3   viewDir = quat_rotate(u_global.camRotation, f32v3(0, 0, 1));
  const Shading shading = s_shade ? shade_blingphong(normal, viewDir) : shade_flat();

  out_color = (diffuse * shading.ambient) +
              (diffuse * c_lightColor * (shading.lambertian + shading.specular));
}
