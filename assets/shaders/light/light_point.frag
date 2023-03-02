#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "pbr.glsl"
#include "texture.glsl"

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_global_img(0) uniform sampler2D u_texGeoData0;
bind_global_img(1) uniform sampler2D u_texGeoData1;
bind_global_img(2) uniform sampler2D u_texGeoDepth;

bind_internal(0) in flat f32v3 in_position;
bind_internal(1) in flat f32v4 in_radianceAndRadiusInv;

bind_internal(0) out f32v3 out_color;

void main() {
  const f32v2      texcoord = in_fragCoord.xy / u_global.resolution.xy;
  const GeoSurface geo =
      geo_surface_load(u_texGeoData0, u_texGeoData1, u_texGeoDepth, texcoord, u_global.viewProjInv);

  const f32v3 viewDir   = normalize(u_global.camPosition.xyz - geo.position);
  const f32v3 radiance  = in_radianceAndRadiusInv.rgb;
  const f32   radiusInv = in_radianceAndRadiusInv.a;

  PbrSurface surf;
  surf.position  = geo.position;
  surf.color     = geo.color;
  surf.normal    = geo.normal;
  surf.roughness = geo.roughness;

  out_color = pbr_light_point(radiance, radiusInv, in_position, viewDir, surf);
}
