#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "color.glsl"
#include "ui.glsl"

bind_spec(0) const u32 s_maxPoints = 1024;
bind_spec(1) const u32 s_pointSize = 4;

struct PointData {
  f32_vec4 points[s_maxPoints];
};

bind_instance_data(0) readonly uniform Instance { PointData u_instance; };

bind_internal(0) out f32_vec4 out_color;

void main() {
  const u32      pointIndex = in_vertexIndex;
  const f32_vec2 point      = u_instance.points[pointIndex].xy;
  const f32      intensity  = u_instance.points[pointIndex].z;

  out_vertexPosition = ui_to_ndc(point);
  out_pointSize      = s_pointSize;
  out_color          = mix(color_white, color_red, intensity);
}
