#ifndef INCLUDE_LIGHT
#define INCLUDE_LIGHT

#include "types.glsl"

const f32v3 c_lightDir       = normalize(f32v3(0.2, 1.0, -0.3));
const f32v3 c_lightColor     = f32v3(1.0, 0.9, 0.7);
const f32   c_lightIntensity = 1.1;
const f32   c_lightShininess = 16;
const f32   c_lightAmbient   = 0.1;

struct Shading {
  f32 lambertian;
  f32 ambient;
  f32 specular;
};

Shading light_shade_flat() {
  Shading res;
  res.lambertian = 0.0;
  res.ambient    = 1.0;
  res.specular   = 0.0;
  return res;
}

Shading light_shade_blingphong(const f32v3 normal, const f32v3 viewDir) {
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

f32v3 light_color(const Shading shading, const f32v3 color) {
  return (color * shading.ambient) +
         (color * c_lightColor * (shading.lambertian + shading.specular));
}

f32v3 light_color_rough(const Shading shading, const f32v3 color, const f32 rough) {
  return (color * shading.ambient) +
         (color * c_lightColor * (shading.lambertian + shading.specular * (1.0 - rough)));
}

#endif // INCLUDE_LIGHT
