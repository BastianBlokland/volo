#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"

const f32v4 c_unitPositions[] = {
    f32v4(-1, 1, 1, 1),
    f32v4(1, 1, 1, 1),
    f32v4(1, -1, 1, 1),
    f32v4(-1, -1, 1, 1),
};
const f32v2 c_unitTexCoords[] = {
    f32v2(0, 0),
    f32v2(1, 0),
    f32v2(1, 1),
    f32v2(0, 1),
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };

bind_internal(0) out f32v2 out_texcoord;

void main() {
  const f32   aspectRatio = u_global.resolution.z;
  const f32v2 size        = aspectRatio < 1 ? f32v2(1, 1 * aspectRatio) : f32v2(1 / aspectRatio, 1);

  out_vertexPosition = c_unitPositions[in_vertexIndex] * f32v4(size, 0, 1);
  out_texcoord       = c_unitTexCoords[in_vertexIndex];
}
