#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"

// Fullscreen at infinite depth.
const f32v4 c_positions[] = {
    f32v4(-1, 1, 0, 1),
    f32v4(1, 1, 0, 1),
    f32v4(1, -1, 0, 1),
    f32v4(-1, -1, 0, 1),
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };

bind_internal(0) out f32v3 out_worldViewDir; // NOTE: non-normalized

void main() {
  const f32m4 clipToWorldSpace = inverse(u_global.viewProj);

  out_vertexPosition = c_positions[in_vertexIndex];
  out_worldViewDir   = (clipToWorldSpace * out_vertexPosition).xyz;
}
