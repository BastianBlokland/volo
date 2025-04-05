#ifndef INCLUDE_PBR
#define INCLUDE_PBR

#include "geometry.glsl"

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

  // NOTE: Minimum of 0.0001 to avoid NaN's and very bright fireflies.
  return nom / max(c_pi * denom * denom, 0.0001);
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
  const f32v3 r = 1.0 - reflectance;
  return reflectance + r * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

/**
 * Compute the ratio of light that gets reflected over the light that gets refracted.
 *
 * Approximates fresnel attenuation based on the roughness as described in:
 * https://seblagarde.wordpress.com/2011/08/17/hello-world/
 */
f32v3 pbr_fresnel_schlick_atten(const f32 cosTheta, const f32v3 reflectance, const f32 roughness) {
  const f32v3 r = max(f32v3(1.0 - roughness), reflectance) - reflectance;
  return reflectance + r * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

f32v3 pbr_reflectance(const f32v3 color, const f32 metalness) {
  /**
   * Calculate reflectance at normal incidence; if dia-electric (like plastic) use uniform
   * reflectance of 0.04 and if it's a metal, use the albedo color (metallic workflow).
   */
  return mix(f32v3(0.04), color, metalness);
}

f32 pbr_attenuation_resolve(const f32 dist, const f32 radiusInv) {
  /**
   * Compute the light attenuation using the inverse square falloff with an artificial radius to
   * force the attenuation to reach 0.
   * Based on the falloff under 'Lighting Model' from 'Real Shading in Unreal Engine 4':
   * https://www.gamedevs.org/uploads/real-shading-in-unreal-engine-4.pdf
   */
  const f32 edgeFrac       = dist * radiusInv;
  const f32 edgeFracQuad   = edgeFrac * edgeFrac * edgeFrac * edgeFrac;
  const f32 centerFracQuad = clamp(1.0 - edgeFracQuad, 0.0, 1.0);
  return centerFracQuad * centerFracQuad / (dist * dist + 1.0);
}

f32 pbr_attenuation_resolve_angle(const f32 lightDot, const f32 angleCos) {
  if (lightDot <= angleCos) {
    return 0.0;
  }
  // Linearly attenuate based on the reference angle.
  return 1.0 - (1.0 - lightDot) * (1.0 / (1.0 - angleCos));
}

struct PbrSurface {
  f32v3 position;
  f32v3 color;
  f32v3 normal;
  f32   roughness, metalness;
};

f32v3 pbr_light_dir(
    const f32v3 radiance, const f32v3 dir, const f32v3 viewDir, const PbrSurface surf) {

  const f32v3 halfDir     = normalize(viewDir - dir);
  const f32v3 reflectance = pbr_reflectance(surf.color, surf.metalness);

  // Cook-Torrance BRDF.
  const f32   normDistFrac = pbr_distribution_ggx(surf.normal, halfDir, surf.roughness);
  const f32   geoFrac      = pbr_geometry_smith(surf.normal, viewDir, -dir, surf.roughness);
  const f32v3 fresnel      = pbr_fresnel_schlick(max(dot(halfDir, viewDir), 0.0), reflectance);

  const f32v3 numerator = normDistFrac * geoFrac * fresnel;
  f32 denominator = 4.0 * max(dot(surf.normal, viewDir), 0.0) * max(dot(surf.normal, -dir), 0.0);
  denominator += 0.0001; // + 0.0001 to prevent divide by zero.
  const f32v3 specular = numerator / denominator;

  // kS is equal to Fresnel.
  const f32v3 kS = fresnel;

  // For energy conservation, the diffuse and specular light can't be above 1.0 (unless the surface
  // emits light). To preserve this relationship the diffuse component (kD) should equal 1.0 - kS.
  f32v3 kD = f32v3(1.0) - kS;

  // Multiply kD by the inverse metalness such that only non-metals have diffuse lighting, or a
  // linear blend if partly metal (pure metals have no diffuse light).
  kD *= 1.0 - surf.metalness;

  // Scale light by NdotL.
  const f32 normDotDir = max(dot(surf.normal, -dir), 0.0);

  // NOTE: We already multiplied the BRDF by the Fresnel (kS) so we won't multiply by kS again.
  return (kD * surf.color / c_pi + specular) * radiance * normDotDir;
}

f32v3 pbr_light_point(
    const f32v3      radiance,
    const f32        radiusInv,
    const f32v3      pos,
    const f32v3      viewDir,
    const PbrSurface surf) {

  const f32v3 lightDir          = normalize(surf.position - pos);
  const f32   dist              = length(surf.position - pos);
  const f32v3 effectiveRadiance = radiance * pbr_attenuation_resolve(dist, radiusInv);
  return pbr_light_dir(effectiveRadiance, lightDir, viewDir, surf);
}

f32v3 pbr_light_spot(
    const f32v3      radiance,
    const f32        lengthInv,
    const f32        angleCos,
    const f32v3      pos,
    const f32v3      dir,
    const f32v3      viewDir,
    const PbrSurface surf) {

  const f32v3 toSurface     = surf.position - pos;
  const f32   distToSurface = length(toSurface);
  const f32v3 dirToSurface  = toSurface / distToSurface;
  const f32   lightDot      = dot(dirToSurface, dir);

  f32v3 effectiveRadiance = radiance;
  effectiveRadiance *= pbr_attenuation_resolve(distToSurface, lengthInv); // Attenuate over dist.
  effectiveRadiance *= pbr_attenuation_resolve_angle(lightDot, angleCos); // Attenuate over angle.

  return pbr_light_dir(effectiveRadiance, dir, viewDir, surf);
}

#define PBR_LIGHT_LINE_ITERATIVE 0

f32v3 pbr_light_line(
    f32v3            radiance,
    const f32        radiusInv,
    f32v3            posA,
    f32v3            posB,
    const f32v3      viewDir,
    const PbrSurface surf) {

#if PBR_LIGHT_LINE_ITERATIVE
  const u32 samples = 64;
  radiance /= samples;

  f32v3 result   = f32v3(0);
  f32v3 posDelta = (posB - posA) / samples;
  f32v3 pos      = posA;
  for (u32 i = 0; i != samples; ++i) {
    const f32v3 lightDir          = normalize(surf.position - pos);
    const f32   dist              = length(surf.position - pos);
    const f32v3 effectiveRadiance = radiance * pbr_attenuation_resolve(dist, radiusInv);
    result += pbr_light_dir(effectiveRadiance, lightDir, viewDir, surf);
    pos += posDelta;
  }
  return result;
#else

  // Clamp the line so it doesn't go inside the surface.
  math_line_clamp_plane(posA, posB, surf.position, surf.normal);

  const f32 distToA = length(posA - surf.position);
  const f32 distToB = length(posB - surf.position);

  /**
   * Approximate the light direction by picking a single point on the line that is most
   * representative of the light coming from the line.
   *
   * Compute the point on the line where the half vector of the surface to the line endpoints
   * intersects with the line.
   * TODO: Split the diffuse and specular directions as this is only reasonable for the diffuse.
   *
   * References:
   * - https://www.elopezr.com/rendering-line-lights/
   */
  const f32   representativeFrac  = clamp(distToA / (distToB + distToA), 0, 1);
  const f32v3 representativePoint = posA + (posB - posA) * representativeFrac;
  const f32v3 lightDir            = normalize(surf.position - representativePoint);

  const f32v3 closestPos = math_line_closest_point(posA, posB, surf.position);
  const f32   dist       = length(surf.position - closestPos);

  const f32v3 effectiveRadiance = radiance * pbr_attenuation_resolve(dist, radiusInv);
  return pbr_light_dir(effectiveRadiance, lightDir, viewDir, surf);
#endif
}

#endif // INCLUDE_PBR
