#version 450
#extension GL_GOOGLE_include_directive : enable

#include "include/binding.glsl"
#include "include/global.glsl"

const f32_vec4 c_colorNormal    = f32_vec4(0.5, 0.5, 0.5, 1.0);
const f32_vec4 c_colorHighlight = f32_vec4(1.0, 1.0, 1.0, 1.0);

bind_spec(0) const i32 s_segments          = 250;
bind_spec(1) const i32 s_highlightInterval = 5;

bind_global_data(0) readonly uniform Global { GlobalData u_global; };

bind_internal(0) out flat f32_vec4 out_color;

void main() {
  const i32 centerX      = i32(u_global.camPosition.x);
  const i32 centerZ      = i32(u_global.camPosition.z);
  const i32 halfSegments = s_segments / 2;

  // First half of the vertices we draw horizontal lines and the other half vertical lines.
  const bool isHor = gl_VertexIndex < s_segments * 2;

  // From -halfSegments to +halfSegments increasing by one every 2 vertices.
  const i32 a = ((gl_VertexIndex / 2) % s_segments) - halfSegments;

  // Every vertex ping-pong between -halfSegments and + halfSegments.
  const i32 b = (gl_VertexIndex & 1) * s_segments - halfSegments;

  const bool isHighlight = (abs((isHor ? centerZ : centerX) + a) % s_highlightInterval) == 0;
  out_color              = isHighlight ? c_colorHighlight : c_colorNormal;

  const f32 x = centerX + (isHor ? b : a);
  const f32 z = centerZ + (isHor ? a : b);
  gl_Position = u_global.viewProj * f32_vec4(x, isHighlight ? 0.01 : 0, z, 1);
}
