#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"

bind_internal(0) out f32v2 out_texcoord;

void main() {
  /**
   * Fullscreen triangle at infinite depth.
   * More info: https://www.saschawillems.de/?page_id=2122
   */
  out_texcoord       = f32v2((in_vertexIndex << 1) & 2, in_vertexIndex & 2);
  out_vertexPosition = f32v4(out_texcoord * 2.0 - 1.0, 0.0, 1.0);
}
