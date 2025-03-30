#include "binding.glsl"
#include "global.glsl"
#include "pbr.glsl"
#include "texture.glsl"

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_global_img(4) uniform sampler2D u_texGeoDepth;

bind_internal(0) in flat f32v3 in_positionA;
bind_internal(1) in flat f32v3 in_positionB;
bind_internal(2) in flat f32v4 in_radianceAndRadiusInv;

bind_internal(0) out f32v3 out_color;

void main() {
  const f32v2 texcoord = in_fragCoord.xy / u_global.resolution.xy;
  const f32   depth    = texture(u_texGeoDepth, texcoord).r;

  const f32v3 clipPos   = f32v3(texcoord * 2.0 - 1.0, depth);
  const f32v3 worldPos  = clip_to_world_pos(u_global, clipPos);
  const f32v3 linePos   = math_line_closest_point(in_positionA, in_positionB, worldPos);
  const f32   dist      = length(worldPos - linePos);
  const f32v3 radiance  = in_radianceAndRadiusInv.rgb;
  const f32   radiusInv = in_radianceAndRadiusInv.a;

  const f32 frac = pbr_attenuation_resolve(dist, radiusInv);
  out_color      = mix(f32v3(0, 0, 0), normalize(radiance), frac);
}
