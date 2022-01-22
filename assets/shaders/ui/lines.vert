#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "color.glsl"
#include "global.glsl"
#include "ui.glsl"

bind_spec(0) const u32 s_maxLines = 1024;

struct LineData {
  f32_vec4 lines[s_maxLines];
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_instance_data(0) readonly uniform Instance { LineData u_instance; };

bind_internal(0) out f32_vec4 out_color;

void main() {
  const u32 lineIndex  = in_vertexIndex / 2;
  const u32 pointIndex = in_vertexIndex % 2; // Either 0, 1 for start, end.

  const f32_vec4 line  = u_instance.lines[lineIndex];
  const f32_vec2 point = pointIndex == 0 ? line.xy : line.zw;

  out_vertexPosition = ui_norm_to_ndc(point * u_global.resolution.zw);
  out_color          = color_white;
}
