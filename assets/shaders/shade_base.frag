#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "tags.glsl"
#include "texture.glsl"

const f32v3 c_sunDir           = normalize(f32v3(0.2, 1.0, -0.3));
const f32v3 c_sunColor         = f32v3(1.0, 0.9, 0.7);
const f32   c_sunIntensity     = 1.1;
const f32   c_ambientIntensity = 0.1;
const f32   c_shininess        = 16;
const f32   c_reflectFrac      = 0.25;

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_global(1) uniform sampler2D u_texGeoColorRough;
bind_global(2) uniform sampler2D u_texGeoNormalTags;
bind_global(3) uniform sampler2D u_texGeoDepth;

bind_graphic(0) uniform samplerCube u_cubeRefl;

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v4 out_color;

f32v3 clip_to_world(const f32v3 clipPos) {
  const f32v4 v = u_global.viewProjInv * f32v4(clipPos, 1);
  return v.xyz / v.w;
}

struct Shading {
  f32 lambertian;
  f32 ambient;
  f32 specular;
};

Shading light_shade_blingphong(const f32v3 normal, const f32v3 viewDir) {
  Shading res;
  res.lambertian = max(dot(normal, c_sunDir), 0.0) * c_sunIntensity;
  res.ambient    = c_ambientIntensity;
  res.specular   = 0.0;
  if (res.lambertian > 0.0) {
    const f32v3 halfDir   = normalize(c_sunDir - viewDir);
    const f32   specAngle = max(dot(normal, halfDir), 0.0);
    res.specular          = pow(specAngle, c_shininess) * c_sunIntensity;
  }
  return res;
}

f32v3 color_with_refl(
    const f32v3 color, const f32 smoothness, const f32v3 normal, const f32v3 viewDir) {
  const f32v3 dir = reflect(viewDir, normal);
  return mix(color, texture_cube(u_cubeRefl, dir).rgb, c_reflectFrac * smoothness);
}

f32v3 color_with_light(const Shading shading, const f32v3 color, const f32 smoothness) {
  return (color * shading.ambient) +
         (color * c_sunColor * (shading.lambertian + shading.specular * smoothness));
}

void main() {
  const f32v4 colorRough = texture(u_texGeoColorRough, in_texcoord);
  const f32v4 normalTags = texture(u_texGeoNormalTags, in_texcoord);
  const f32   depth      = texture(u_texGeoDepth, in_texcoord).r;

  const f32v3 color      = colorRough.rgb;
  const f32   roughness  = colorRough.a;
  const f32   smoothness = 1.0 - roughness;
  const f32v3 normal     = normalTags.xyz;
  const u32   tags       = tags_tex_decode(normalTags.w);

  const f32v3 clipPos  = f32v3(in_texcoord * 2.0 - 1.0, depth);
  const f32v3 worldPos = clip_to_world(clipPos);
  const f32v3 viewDir  = normalize(worldPos - u_global.camPosition.xyz);

  const Shading shading = light_shade_blingphong(normal, viewDir);

  const f32v3 colorWithRefl  = color_with_refl(color, smoothness, normal, viewDir);
  const f32v3 colorWithLight = color_with_light(shading, colorWithRefl, smoothness);

  out_color = f32v4(colorWithLight, 1.0);
}
