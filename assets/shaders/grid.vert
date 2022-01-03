#version 450
#extension GL_GOOGLE_include_directive : enable

#include "include/binding.glsl"
#include "include/global.glsl"

const i32      c_segments          = 200;
const i32      c_highlightInterval = 5;
const f32_vec4 c_colorNormal       = f32_vec4(0.5, 0.5, 0.5, 0.2);
const f32_vec4 c_colorHighlight    = f32_vec4(0.8, 0.8, 0.8, 0.4);

bind_global_data(0) readonly uniform Global { GlobalData u_global; };

bind_internal(0) out f32_vec3 out_gridPos;
bind_internal(1) out flat f32_vec4 out_color;

void main() {
  const i32 centerX      = i32(u_global.camPosition.x);
  const i32 centerZ      = i32(u_global.camPosition.z);
  const i32 halfSegments = c_segments / 2;

  // First half of the vertices we draw horizontal lines and the other half vertical lines.
  const bool isHorizontal = gl_VertexIndex < c_segments * 2;

  // From -halfSegments to +halfSegments increasing by one every 2 vertices.
  const i32 a = ((gl_VertexIndex / 2) % c_segments) - halfSegments;

  // Every vertex ping-pong between -halfSegments and + halfSegments.
  const i32 b = (gl_VertexIndex & 1) * c_segments - halfSegments;

  const bool isHighlight = (abs((isHorizontal ? centerZ : centerX) + a) % c_highlightInterval) == 0;
  out_color              = isHighlight ? c_colorHighlight : c_colorNormal;

  const f32 x = centerX + (isHorizontal ? b : a);
  const f32 z = centerZ + (isHorizontal ? a : b);
  out_gridPos = f32_vec3(x, 0, z);

  gl_Position = u_global.viewProj * f32_vec4(x, isHighlight ? 0.01 : 0, z, 1);
}
