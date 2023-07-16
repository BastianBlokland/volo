#ifndef INCLUDE_MATH_FRAG
#define INCLUDE_MATH_FRAG

#include "math.glsl"

/**
 * Apply a tangent normal (from a normalmap for example) to a worldNormal.
 * The tangent and bitangent vectors are derived from change in position and texcoords.
 */
f32v3 math_perturb_normal(
    const f32v3 tangentNormal,
    const f32v3 worldNormal,
    const f32v3 worldPos,
    const f32v2 texcoord) {
  const f32v3 deltaPosX = dFdx(worldPos);
  const f32v3 deltaPosY = dFdy(worldPos);
  const f32v2 deltaTexX = dFdx(texcoord);
  const f32v2 deltaTexY = dFdy(texcoord);

  const f32v3 refNorm  = normalize(worldNormal);
  const f32v3 refTan   = normalize(deltaPosX * deltaTexY.t - deltaPosY * deltaTexX.t);
  const f32v3 refBitan = normalize(cross(refNorm, refTan));
  const f32m3 rot      = f32m3(refTan, refBitan, refNorm);

  return normalize(rot * tangentNormal);
}

#endif // INCLUDE_MATH_FRAG
