#include "binding.glsl"
#include "color.glsl"
#include "global.glsl"
#include "instance.glsl"
#include "math.glsl"

const f32 c_thickness = 0.2;

struct RingInstanceData {
  f32v4 data1; // x, y, z: center, w: radius
  u32v4 data2; // x: vertexCount, y: color, z, w: unused
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_instance_data(0) readonly uniform Instance { RingInstanceData[c_maxInstances] u_instances; };

bind_internal(0) out flat f32v4 out_color;
bind_internal(1) out f32 out_radiusFrac;

void main() {
  const f32v3 instanceCenter      = u_instances[in_instanceIndex].data1.xyz;
  const f32   instanceRingRadius  = u_instances[in_instanceIndex].data1.w;
  const u32   instanceVertexCount = u_instances[in_instanceIndex].data2.x;
  const f32v4 instanceColor       = color_from_u32(u_instances[in_instanceIndex].data2.y);

  const f32   radiusFrac = in_vertexIndex & 1; // Alternate between 0 and 1.
  const f32   radius     = instanceRingRadius + radiusFrac * c_thickness - (c_thickness * 0.5);
  const f32   angle      = (in_vertexIndex / f32(instanceVertexCount - 2)) * c_pi * 2.0;
  const f32v3 worldPos   = instanceCenter + f32v3(radius * sin(angle), 0, radius * cos(angle));

  out_vertexPosition = u_global.viewProj * f32v4(worldPos, 1);
  out_color          = instanceColor;
  out_radiusFrac     = radiusFrac;
}
