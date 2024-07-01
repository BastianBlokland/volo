#include "binding.glsl"
#include "color.glsl"
#include "global.glsl"
#include "instance.glsl"
#include "math.glsl"

const f32   c_thickness  = 0.2;
const f32v3 c_corners[4] = {
    f32v3(-0.5, 0.0, +0.5),
    f32v3(-0.5, 0.0, -0.5),
    f32v3(+0.5, 0.0, -0.5),
    f32v3(+0.5, 0.0, +0.5),
};

struct BoxInstanceData {
  f32v4 data1; // x, y, z: center, w: unused
  f32v4 data2; // x: width, y: height, z: color, w: unused
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_instance_data(0) readonly uniform Instance { BoxInstanceData[c_maxInstances] u_instances; };

bind_internal(0) out flat f32v4 out_color;
bind_internal(1) out f32 out_radiusFrac;

void main() {
  const f32v3 instanceCenter = u_instances[in_instanceIndex].data1.xyz;
  const f32   instanceWidth  = u_instances[in_instanceIndex].data2.x;
  const f32   instanceHeight = u_instances[in_instanceIndex].data2.y;
  const f32v2 instanceSize   = f32v2(instanceWidth, instanceHeight);
  const u32   instanceColor  = floatBitsToUint(u_instances[in_instanceIndex].data2.z);

  const f32 radiusFrac  = in_vertexIndex & 1; // Alternate between 0 and 1.
  const u32 cornerIndex = (in_vertexIndex % 8) / 2;

  const f32v2 size     = instanceSize + (radiusFrac - 0.5) * c_thickness * 2.0;
  const f32v3 worldPos = instanceCenter + c_corners[cornerIndex] * f32v3(size.x, 0, size.y);

  out_vertexPosition = u_global.viewProj * f32v4(worldPos, 1);
  out_color          = color_from_u32(instanceColor);
  out_radiusFrac     = radiusFrac;
}
