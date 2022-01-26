#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "color.glsl"
#include "global.glsl"
#include "ui.glsl"

bind_spec(0) const u32 s_maxPoints = 1024;
bind_spec(1) const u32 s_pointSize = 4;

struct PointData {
  f32v4 points[s_maxPoints];
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_instance_data(0) readonly uniform Instance { PointData u_instance; };

bind_internal(0) out f32v4 out_color;

void main() {
  const u32   pointIndex = in_vertexIndex;
  const f32v2 point      = u_instance.points[pointIndex].xy;
  const f32   intensity  = u_instance.points[pointIndex].z;

  out_vertexPosition = ui_norm_to_ndc(point * u_global.resolution.zw);
  out_pointSize      = s_pointSize;
  out_color          = mix(color_white, color_red, intensity);
}
