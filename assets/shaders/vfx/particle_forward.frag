#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "texture.glsl"

const f32 c_geometryFadeDist = 2.0;

const u32 c_flagGeometryFade = 1 << 0;

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_global_img(2) uniform sampler2D u_texGeoDepth;

bind_graphic_img(0) uniform sampler2D u_atlas;

bind_internal(0) in flat f32v4 in_color;
bind_internal(1) in flat f32 in_opacity;
bind_internal(2) in flat u32 in_flags;
bind_internal(3) in f32v2 in_texcoord;

bind_internal(0) out f32v4 out_color;

void main() {
  const f32v4 texSample = texture(u_atlas, in_texcoord);

  f32 alpha = texSample.a * in_color.a;

  if ((in_flags & c_flagGeometryFade) != 0) {
    const f32v2 texcoord         = in_fragCoord.xy / u_global.resolution.xy;
    const f32v2 clipXY           = texcoord * 2.0 - 1.0;
    const f32   sceneDepth       = texture(u_texGeoDepth, texcoord).r;
    const f32   sceneLinearDepth = clip_to_view_depth(u_global, f32v3(clipXY, sceneDepth));
    const f32   fragLinearDepth  = clip_to_view_depth(u_global, f32v3(clipXY, in_fragCoord.z));

    alpha *= smoothstep(0, 1, (sceneLinearDepth - fragLinearDepth) / c_geometryFadeDist);
  }

  out_color.rgb = texSample.rgb * in_color.rgb * alpha;
  out_color.a   = clamp(alpha * in_opacity, 0, 1);
}
