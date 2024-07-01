#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "texture.glsl"

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_graphic_img(0) uniform sampler2D u_atlas;

bind_internal(0) in flat f32v4 in_color;
bind_internal(3) in f32v2 in_texcoord;
bind_internal(4) in f32v3 in_worldPosition;

bind_internal(0) out f32v2 out_distortion;

f32v2 current_clip_pos() { return in_fragCoord.xy / u_global.resolution.xy * 2.0 - 1.0; }

void main() {
  const f32v4 texSample       = texture(u_atlas, in_texcoord);
  const f32v3 distortNormal   = view_to_world_dir(u_global, normal_tex_decode(texSample.rgb));
  const f32   distortStrength = texSample.a * in_color.a;

  const f32v3 distortWorldPos = in_worldPosition + distortNormal * distortStrength;
  const f32v2 distortClipPos  = world_to_clip_pos(u_global, distortWorldPos);

  out_distortion = distortClipPos - current_clip_pos();
}
