#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"

struct GridData {
  f32 cellSize;
  u32 segmentCount;
  u32 highlightInterval;
  f32 fadeFraction;
};

const f32v4 c_colorNormal      = f32v4(0.5, 0.5, 0.5, 0.2);
const f32v4 c_colorHighlight   = f32v4(0.8, 0.8, 0.8, 0.4);
const f32   c_highlightYOffset = 0.0005;

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_instance_data(0) readonly uniform Instance { GridData u_instance; };

bind_internal(0) out f32v3 out_worldGridPos;
bind_internal(1) out flat f32 out_gridHalfSize;
bind_internal(2) out flat f32v4 out_color;
bind_internal(3) out flat f32 out_fadeFraction;

void main() {
  const f32 invCellSize  = 1.0 / u_instance.cellSize;
  const i32 centerX      = i32(u_global.camPosition.x * invCellSize);
  const i32 centerZ      = i32(u_global.camPosition.z * invCellSize);
  const i32 segments     = i32(u_instance.segmentCount);
  const i32 halfSegments = segments / 2;

  // First half of the vertices we draw horizontal lines and the other half vertical lines.
  const bool isHorizontal = in_vertexIndex < segments * 2;

  // From -halfSegments to +halfSegments increasing by one every 2 vertices.
  const i32 a = ((in_vertexIndex / 2) % segments) - halfSegments;

  // Every vertex ping-pong between -halfSegments and + halfSegments.
  const i32 b = (in_vertexIndex & 1) * segments - halfSegments;

  const f32 x      = (centerX + (isHorizontal ? b : a)) * u_instance.cellSize;
  const f32 z      = (centerZ + (isHorizontal ? a : b)) * u_instance.cellSize;
  out_worldGridPos = f32v3(x, 0, z);
  out_gridHalfSize = segments * u_instance.cellSize * 0.5;

  const bool isHighlight =
      (abs((isHorizontal ? centerZ : centerX) + a) % u_instance.highlightInterval) == 0;
  out_color = isHighlight ? c_colorHighlight : c_colorNormal;

  out_vertexPosition = u_global.viewProj * f32v4(x, f32(isHighlight) * c_highlightYOffset, z, 1);
  out_fadeFraction   = u_instance.fadeFraction;
}
