#ifndef INCLUDE_UI
#define INCLUDE_UI

#include "types.glsl"

/**
 * Map from UI to  NDC coordinates.
 *
 * Input:
 *   0,1        1,1
 *   0,0        1,0
 *
 * Output:
 *   -1,-1,0,1  1,-1,0,1
 *   -1,1,0,1   1,1,0,1
 */
f32_vec4 ui_to_ndc(const f32_vec2 uiPosition) {
  return f32_vec4(uiPosition * f32_vec2(2, -2) - f32_vec2(1, -1), 0.0, 1.0);
}

#endif // INCLUDE_UI
