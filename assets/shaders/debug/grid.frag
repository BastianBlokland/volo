#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "tag.glsl"

const f32 c_behindThreshold = 0.1;
const f32 c_behindAlphaMul  = 0.15;

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_global_img(1) uniform sampler2D u_texGeoData1;
bind_global_img(2) uniform sampler2D u_texGeoDepth;

bind_internal(0) in f32v3 in_worldGridPos;
bind_internal(1) in flat f32 in_gridHalfSize;
bind_internal(2) in flat f32v4 in_color;
bind_internal(3) in flat f32 in_fadeFraction;

bind_internal(0) out f32v4 out_color;

f32 compute_fade(const f32v3 center) {
  const f32 dist = length(in_worldGridPos - center);
  return 1.0 - smoothstep(in_gridHalfSize * (1.0 - in_fadeFraction), in_gridHalfSize, dist);
}

void main() {
  const f32v2 texcoord         = in_fragCoord.xy / u_global.resolution.xy;
  const f32v2 clipXY           = texcoord * 2.0 - 1.0;
  const u32   sceneTags        = tags_tex_decode(texture(u_texGeoData1, texcoord).w);
  const f32   sceneDepth       = texture(u_texGeoDepth, texcoord).r;
  const f32   sceneLinearDepth = clip_to_view_depth(u_global, f32v3(clipXY, sceneDepth));
  const f32   fragLinearDepth  = clip_to_view_depth(u_global, f32v3(clipXY, in_fragCoord.z));

  f32 alpha = in_color.a;
  if ((fragLinearDepth - sceneLinearDepth) >= c_behindThreshold) {
    // This fragment is behind geometry; if we are over terrain draw it dimmed, otherwise discard.
    if (tag_is_set(sceneTags, tag_terrain_bit)) {
      alpha *= 0.5;
    } else {
      discard;
    }
  }
  alpha *= compute_fade(u_global.camPosition.xyz);

  out_color = f32v4(in_color.rgb, alpha);
}
