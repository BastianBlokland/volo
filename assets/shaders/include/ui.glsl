#ifndef INCLUDE_UI
#define INCLUDE_UI

#include "types.glsl"

/**
 * Map from normalized UI coordinates to NDC coordinates.
 *
 * Input:
 *   0,1        1,1
 *   0,0        1,0
 *
 * Output:
 *   -1,-1,1,1  1,-1,1,1
 *   -1,1,1,1   1,1,1,1
 *
 * NOTE: Output depth is 0 meaning right at the camera.
 */
f32v4 ui_norm_to_ndc(const f32v2 uiPosition) {
  return f32v4(uiPosition * f32v2(2, -2) - f32v2(1, -1), 1.0, 1.0);
}

#endif // INCLUDE_UI
