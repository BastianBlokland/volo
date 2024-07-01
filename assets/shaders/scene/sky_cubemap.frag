#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "texture.glsl"

bind_graphic_img(0) uniform samplerCube u_texCubeMap;

bind_internal(0) in f32v3 in_worldViewDir; // NOTE: non-normalized

bind_internal(0) out f32v3 out_color;

void main() { out_color = texture_cube(u_texCubeMap, in_worldViewDir).rgb; }
