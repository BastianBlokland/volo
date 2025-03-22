#include "binding.glsl"
#include "global.glsl"
#include "pbr.glsl"
#include "texture.glsl"

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_global_img(0) uniform sampler2D u_texGeoBase;
bind_global_img(1) uniform sampler2D u_texGeoNormal;
bind_global_img(2) uniform sampler2D u_texGeoAttribute;
bind_global_img(4) uniform sampler2D u_texGeoDepth;

bind_internal(0) in flat f32v3 in_position;
bind_internal(1) in flat f32v4 in_radianceAndRadiusInv;

bind_internal(0) out f32v3 out_color;

void main() {
  const f32v2 texcoord = in_fragCoord.xy / u_global.resolution.xy;

  GeometryEncoded geoEncoded; // NOTE: We are not decoding emissive.
  geoEncoded.base   = texture(u_texGeoBase, texcoord);
  geoEncoded.normal = texture(u_texGeoNormal, texcoord).rg;
  geoEncoded.attr   = texture(u_texGeoAttribute, texcoord).rg;

  const Geometry geo = geometry_decode(geoEncoded);

  const f32   depth    = texture(u_texGeoDepth, texcoord).r;
  const f32v3 clipPos  = f32v3(texcoord * 2.0 - 1.0, depth);
  const f32v3 worldPos = clip_to_world_pos(u_global, clipPos);
  const f32v3 viewDir  = normalize(u_global.camPosition.xyz - worldPos);

  const f32v3 radiance  = in_radianceAndRadiusInv.rgb;
  const f32   radiusInv = in_radianceAndRadiusInv.a;

  PbrSurface surf;
  surf.position  = worldPos;
  surf.color     = geo.color;
  surf.normal    = geo.normal;
  surf.roughness = geo.roughness;

  out_color = pbr_light_point(radiance, radiusInv, in_position, viewDir, surf);
}
