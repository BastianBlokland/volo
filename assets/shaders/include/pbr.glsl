#ifndef INCLUDE_PBR
#define INCLUDE_PBR

#include "math.glsl"
#include "types.glsl"

/**
 * Physically based lighting utilities.
 * Information can be found in the great LearnOpenGL chapters:
 * - https://learnopengl.com/PBR/Theory
 * - https://learnopengl.com/PBR/Lighting
 */

/**
 * Trowbridge-Reitz normal distribution function.
 * Statistically approximates the relative surface area of microfacets exactly aligned to the
 * halfway vector.
 */
f32 pbr_distribution_ggx(const f32v3 normal, const f32v3 halfDir, const f32 roughness) {
  const f32 a            = roughness * roughness;
  const f32 a2           = a * a;
  const f32 normDotHalf  = max(dot(normal, halfDir), 0.0);
  const f32 normDotHalf2 = normDotHalf * normDotHalf;

  const f32 nom   = a2;
  const f32 denom = (normDotHalf2 * (a2 - 1.0) + 1.0);

  return nom / (c_pi * denom * denom);
}

f32 pbr_geometry_schlick_ggx(const f32 normDotView, const f32 roughness) {
  const f32 r = (roughness + 1.0);
  const f32 k = (r * r) / 8.0;

  const f32 nom   = normDotView;
  const f32 denom = normDotView * (1.0 - k) + k;

  return nom / denom;
}

/**
 * Statistically approximates the relative surface area where its micro surface-details overshadow
 * each other, causing light rays to be occluded.
 */
f32 pbr_geometry_smith(
    const f32v3 normal, const f32v3 view, const f32v3 lightDir, const f32 roughness) {
  const f32 normDotView  = max(dot(normal, view), 0.0);
  const f32 normDotLight = max(dot(normal, lightDir), 0.0);
  const f32 ggx2         = pbr_geometry_schlick_ggx(normDotView, roughness);
  const f32 ggx1         = pbr_geometry_schlick_ggx(normDotLight, roughness);
  return ggx1 * ggx2;
}

/**
 * Compute the ratio of light that gets reflected over the light that gets refracted.
 */
f32v3 pbr_fresnel_schlick(const f32 cosTheta, const f32v3 reflectance) {
  return reflectance + (1.0 - reflectance) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

struct PbrSurface {
  f32v3 position;
  f32v3 color;
  f32v3 normal;
  f32   roughness;
  f32   metallicness;
};

f32v3 pbr_surf_reflectance(const PbrSurface surf) {
  /**
   * Calculate reflectance at normal incidence; if dia-electric (like plastic) use uniform
   * reflectance of of 0.04 and if it's a metal, use the albedo color (metallic workflow).
   */
  return mix(f32v3(0.04), surf.color, surf.metallicness);
}

f32 pbr_attenuation_resolve(const f32v3 attenuation, const f32 dist) {
  const f32 c = attenuation.x; // Constant term.
  const f32 l = attenuation.y; // Linear term.
  const f32 q = attenuation.z; // Quadratic term.
  return 1.0 / (c + l * dist + q * dist * dist);
}

f32v3 pbr_light_dir(
    const f32v3 radiance, const f32v3 dir, const f32v3 viewDir, const PbrSurface surf) {

  const f32v3 halfDir     = normalize(viewDir - dir);
  const f32v3 reflectance = pbr_surf_reflectance(surf);

  // Cook-Torrance BRDF.
  const f32   normDistFrac = pbr_distribution_ggx(surf.normal, halfDir, surf.roughness);
  const f32   geoFrac      = pbr_geometry_smith(surf.normal, viewDir, -dir, surf.roughness);
  const f32v3 fresnelFrac  = pbr_fresnel_schlick(max(dot(halfDir, viewDir), 0.0), reflectance);

  const f32v3 numerator = normDistFrac * geoFrac * fresnelFrac;
  f32 denominator = 4.0 * max(dot(surf.normal, viewDir), 0.0) * max(dot(surf.normal, -dir), 0.0);
  denominator += 0.0001; // + 0.0001 to prevent divide by zero.
  const f32v3 specular = numerator / denominator;

  // kS is equal to Fresnel.
  const f32v3 kS = fresnelFrac;

  // For energy conservation, the diffuse and specular light can't be above 1.0 (unless the surface
  // emits light). To preserve this relationship the diffuse component (kD) should equal 1.0 - kS.
  f32v3 kD = f32v3(1.0) - kS;

  // Multiply kD by the inverse metalness such that only non-metals have diffuse lighting, or a
  // linear blend if partly metal (pure metals have no diffuse light).
  kD *= 1.0 - surf.metallicness;

  // Scale light by NdotL.
  const f32 normDotDir = max(dot(surf.normal, -dir), 0.0);

  // NOTE: We already multiplied the BRDF by the Fresnel (kS) so we won't multiply by kS again.
  return (kD * surf.color / c_pi + specular) * radiance * normDotDir;
}

f32v3 pbr_light_point(
    const f32v3      radiance,
    const f32v3      pos,
    const f32v3      attenuation, // x: constant, y: linear, z: quadratic
    const f32v3      viewDir,
    const PbrSurface surf) {

  const f32v3 lightDir          = normalize(surf.position - pos);
  const f32   dist              = length(surf.position - pos);
  const f32v3 effectiveRadiance = radiance * pbr_attenuation_resolve(attenuation, dist);
  return pbr_light_dir(effectiveRadiance, lightDir, viewDir, surf);
}

#endif // INCLUDE_PBR
