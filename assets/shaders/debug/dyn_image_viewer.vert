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
bind_dynamic(1) uniform sampler2D u_tex;

bind_internal(0) out f32v2 out_texcoord;

f32 texture_aspect(const sampler2D texture) {
  const f32v2 size = textureSize(texture, 0);
  return size.x / size.y;
}

void main() {
  const f32 screenAspect  = u_global.resolution.z;
  const f32 textureAspect = texture_aspect(u_tex);

  /**
   * Scale the image so that it fills the screen while maintaining the aspect ratio.
   */
  const f32   aspectFrac = textureAspect / screenAspect;
  const f32v2 size       = aspectFrac > 1 ? f32v2(1, 1 / aspectFrac) : f32v2(aspectFrac, 1);

  out_vertexPosition = c_unitPositions[in_vertexIndex] * f32v4(size, 0, 1);
  out_texcoord       = c_unitTexCoords[in_vertexIndex];
}
