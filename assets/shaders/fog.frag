#include "binding.glsl"
#include "global.glsl"
#include "texture.glsl"

struct FogData {
  f32m4 fogViewProj;
};

const f32v4 c_fogColor = f32v4(0.005, 0.005, 0.0075, 0.95);

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_global_img(2) uniform sampler2D u_texGeoDepth;
bind_draw_data(0) readonly uniform Draw { FogData u_draw; };
bind_draw_img(0) uniform sampler2D u_fogMap;

bind_internal(0) in f32v2 in_texcoord;

bind_internal(0) out f32v4 out_color;

f32v3 fog_map_coord(const f32v3 worldPos) {
  const f32v4 clipPos = u_draw.fogViewProj * f32v4(worldPos, 1.0);
  return f32v3(clipPos.xy * 0.5 + 0.5, clipPos.z);
}

f32 fog_frac(const f32v3 worldPos) {
  const f32v3 fogCoord = fog_map_coord(worldPos);
  if (fogCoord.z <= 0.0) {
    return 1.0;
  }
  return 1.0 - texture(u_fogMap, fogCoord.xy).r;
}

void main() {
  const f32   depth    = texture(u_texGeoDepth, in_texcoord).r;
  const f32v3 clipPos  = f32v3(in_texcoord * 2.0 - 1.0, depth);
  const f32v3 worldPos = clip_to_world_pos(u_global, clipPos);

  const f32 fogFrac = fog_frac(worldPos);
  out_color         = f32v4(c_fogColor.rgb, c_fogColor.a * fogFrac);
}
