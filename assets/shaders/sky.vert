#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"

// Fullscreen at infinite depth.
const f32_vec4 c_positions[] = {
    f32_vec4(-1, 1, 0, 1),
    f32_vec4(1, 1, 0, 1),
    f32_vec4(1, -1, 0, 1),
    f32_vec4(-1, -1, 0, 1),
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };

bind_internal(0) out f32_vec3 out_viewDir; // NOTE: non-normalized

void main() {
  const f32_mat4 clipToWorldSpace = inverse(u_global.viewProj);

  out_vertexPosition = c_positions[in_vertexIndex];
  out_viewDir        = (clipToWorldSpace * out_vertexPosition).xyz;
}
