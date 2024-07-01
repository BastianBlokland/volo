#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"

struct GridData {
  f16 cellSize;
  f16 height;
  u32 cellCount;
  u32 highlightInterval;
};

const f32v4 c_colorNormal      = f32v4(0.2, 0.2, 0.2, 0.6);
const f32v4 c_colorHighlight   = f32v4(0.1, 0.1, 0.1, 0.9);
const f32   c_highlightYOffset = 0.0005;

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_instance_data(0) readonly uniform Instance { GridData u_instance; };

bind_internal(0) out flat f32v4 out_color;

void main() {
  const f32 cellSize     = f32(u_instance.cellSize);
  const f32 invCellSize  = 1.0 / cellSize;
  const i32 segments     = i32(u_instance.cellCount) + 1; // +1 'close' the last row and column.
  const i32 halfSegments = segments / 2;

  // First half of the vertices we draw horizontal lines and the other half vertical lines.
  const bool horizontal = in_vertexIndex < segments * 2;

  // From -halfSegments to +halfSegments increasing by one every 2 vertices.
  const i32 a = ((in_vertexIndex / 2) % segments) - halfSegments;

  // Every vertex ping-pong between -halfSegments and (+halfSegments - 1).
  const i32 b = (in_vertexIndex & 1) * segments - halfSegments - (in_vertexIndex & 1);

  const f32 x = (horizontal ? b : a) * cellSize;
  const f32 z = (horizontal ? a : b) * cellSize;

  const bool highlight = (abs(a) % u_instance.highlightInterval) == 0;
  out_color            = highlight ? c_colorHighlight : c_colorNormal;

  const f32 y = f32(u_instance.height) + f32(highlight) * c_highlightYOffset;

  out_vertexPosition = u_global.viewProj * f32v4(x, y, z, 1);
}
