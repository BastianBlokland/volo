#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "instance.glsl"

struct ShapeInstanceData {
  f32v4 positions[2]; // x, y, z: position
  f32v4 color;        // x, y, z, w: color
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_instance_data(0) readonly uniform Instance { ShapeInstanceData[c_maxInstances] u_instances; };

bind_internal(0) out f32v4 out_color;

void main() {
  const f32v3 instancePos   = u_instances[in_instanceIndex].positions[in_vertexIndex].xyz;
  const f32v4 instanceColor = u_instances[in_instanceIndex].color;

  out_vertexPosition = u_global.viewProj * f32v4(instancePos, 1);
  out_color          = instanceColor;
}
