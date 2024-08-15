#include "binding.glsl"

const f32 c_alpha = 0.25;

bind_internal(0) out f32v4 out_color;

void main() { out_color = f32v4(1, 1, 1, c_alpha); }
