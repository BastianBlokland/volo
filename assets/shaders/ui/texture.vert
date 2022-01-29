#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"

const f32v4 c_positions[] = {
    f32v4(-1, 1, 1, 1),
    f32v4(1, 1, 1, 1),
    f32v4(1, -1, 1, 1),
    f32v4(-1, -1, 1, 1),
};
const f32v2 c_texCoords[] = {
    f32v2(0, 0),
    f32v2(1, 0),
    f32v2(1, 1),
    f32v2(0, 1),
};

bind_internal(0) out f32v2 out_texcoord;

void main() {
  out_vertexPosition = c_positions[in_vertexIndex];
  out_texcoord       = c_texCoords[in_vertexIndex];
}
