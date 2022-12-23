#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "tags.glsl"
#include "texture.glsl"

struct ShadeBaseData {
  f32v4 sunLightShininess; // rgb: sunLight, a: sunShininess.
  f32v3 sunDir;
  f32   ambientIntensity;
  f32   reflectFrac;
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_global(1) uniform sampler2D u_texGeoColorRough;
bind_global(2) uniform sampler2D u_texGeoNormalTags;
bind_global(3) uniform sampler2D u_texGeoDepth;
bind_draw_data(0) readonly uniform Draw { ShadeBaseData u_draw; };

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
  res.lambertian = max(dot(normal, u_draw.sunDir), 0.0);
  res.ambient    = u_draw.ambientIntensity;
  res.specular   = 0.0;
  if (res.lambertian > 0.0) {
    const f32v3 halfDir   = normalize(u_draw.sunDir - viewDir);
    const f32   specAngle = max(dot(normal, halfDir), 0.0);
    res.specular          = pow(specAngle, u_draw.sunLightShininess.a);
  }
  return res;
}

f32v3 color_with_refl(
    const f32v3 color, const f32 smoothness, const f32v3 normal, const f32v3 viewDir) {
  const f32v3 dir = reflect(viewDir, normal);
  return mix(color, texture_cube(u_cubeRefl, dir).rgb, u_draw.reflectFrac * smoothness);
}

f32v3 color_with_light(const Shading shading, const f32v3 color, const f32 smoothness) {
  return (color * shading.ambient) + (color * u_draw.sunLightShininess.rgb *
                                      (shading.lambertian + shading.specular * smoothness));
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

  // TODO: Apply these effects at a later stage (after all the lighting has been done).
  if (tag_is_set(tags, tag_selected_bit)) {
    out_color.rgb += (1.0 - abs(dot(normal, viewDir))) * 2.0;
  }
  if (tag_is_set(tags, tag_damaged_bit)) {
    out_color.rgb = mix(out_color.rgb, f32v3(0.8, 0.1, 0.1), abs(dot(normal, viewDir)));
  }
}
