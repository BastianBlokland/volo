#version 450
#extension GL_GOOGLE_include_directive : enable

#include "include/binding.glsl"
#include "include/color.glsl"
#include "include/ui.glsl"

bind_spec(0) const u32 s_maxLines = 1024;

struct LineData {
  f32_vec4 lines[s_maxLines];
};

bind_instance_data(0) readonly uniform Instance { LineData u_instance; };

bind_internal(0) out f32_vec4 out_color;

void main() {
  const u32 lineIndex  = in_vertexIndex / 2;
  const u32 pointIndex = in_vertexIndex % 2; // Either 0, 1 for start, end.

  const f32_vec4 line  = u_instance.lines[lineIndex];
  const f32_vec2 point = pointIndex == 0 ? line.xy : line.zw;

  out_vertexPosition = ui_to_ndc(point);
  out_color          = color_white;
}
