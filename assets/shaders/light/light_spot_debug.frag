#include "binding.glsl"
#include "global.glsl"
#include "pbr.glsl"
#include "texture.glsl"

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_global_img(4) uniform sampler2D u_texGeoDepth;

bind_internal(0) in flat f32v3 in_position;
bind_internal(1) in flat f32v4 in_directionAndAngleCos;
bind_internal(2) in flat f32v4 in_radianceAndLengthInv;

bind_internal(0) out f32v3 out_color;

void main() {
  const f32v2 texcoord = in_fragCoord.xy / u_global.resolution.xy;
  const f32   depth    = texture(u_texGeoDepth, texcoord).r;

  const f32v3 clipPos   = f32v3(texcoord * 2.0 - 1.0, depth);
  const f32v3 worldPos  = clip_to_world_pos(u_global, clipPos);

  const f32v3 toSurface     = worldPos - in_position;
  const f32   distToSurface = length(toSurface);
  const f32v3 dirToSurface  = toSurface / distToSurface;
  const f32   lightDot      = dot(dirToSurface, in_directionAndAngleCos.xyz);

  const f32   angleCos  = in_directionAndAngleCos.w;
  const f32v3 radiance  = in_radianceAndLengthInv.rgb;
  const f32   lengthInv = in_radianceAndLengthInv.a;

  f32 frac = 1.0;
  frac *= pbr_attenuation_resolve(distToSurface, lengthInv); // Attenuate over dist.
  frac *= pbr_attenuation_resolve_angle(lightDot, angleCos); // Attenuate over angle.

  out_color = mix(f32v3(0, 0, 0), normalize(radiance), frac);
}
