#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"

bind_global_data(0) readonly uniform Global { GlobalData u_global; };

bind_internal(0) out f32v3 out_worldViewDir; // NOTE: non-normalized

void main() {
  const f32m4 clipToWorldSpace = inverse(u_global.viewProj);

  /**
   * Fullscreen triangle at infinite depth.
   * More info: https://www.saschawillems.de/?page_id=2122
   */
  const f32v2 texcoord = f32v2((in_vertexIndex << 1) & 2, in_vertexIndex & 2);
  out_vertexPosition   = f32v4(texcoord * 2.0 - 1.0, 0.0, 1.0);
  out_worldViewDir     = (clipToWorldSpace * out_vertexPosition).xyz;
}
