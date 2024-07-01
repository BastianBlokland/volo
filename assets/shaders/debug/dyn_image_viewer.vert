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

struct ImageData {
  u16 imageChannels;
  f16 lod;
  u32 flags;
  f32 exposure;
  f32 aspect;
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_draw_data(0) readonly uniform Draw { ImageData u_draw; };

bind_internal(0) out f32v2 out_texcoord;

void main() {
  const f32 screenAspect = u_global.resolution.z;
  const f32 imageAspect  = u_draw.aspect;

  /**
   * Rectangle to display the image onto.
   * NOTE: Display the image on the top 75% of the screen to leave some space for UI.
   */
  const f32v2 rectOff    = f32v2(0, -0.2);
  const f32v2 rectSize   = f32v2(1, 0.7);
  const f32   rectAspect = rectSize.x / rectSize.y * screenAspect;

  /**
   * Scale the image so that it fills the rect while maintaining the aspect ratio.
   */
  const f32 aspectFrac = imageAspect / rectAspect;
  f32v2     size;
  if (aspectFrac > 1) {
    size = f32v2(rectSize.x, rectSize.y / aspectFrac);
  } else {
    size = f32v2(rectSize.x * aspectFrac, rectSize.y);
  }

  out_vertexPosition = c_unitPositions[in_vertexIndex] * f32v4(size, 0, 1) + f32v4(rectOff, 0, 0);
  out_texcoord       = c_unitTexCoords[in_vertexIndex];
}
