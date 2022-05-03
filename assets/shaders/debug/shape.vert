#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "instance.glsl"
#include "quat.glsl"
#include "vertex.glsl"

struct ShapeInstanceData {
  f32v4 pos;   // x, y, z: position
  f32v4 rot;   // x, y, z, w: rotation quaternion
  f32v4 scale; // x, y, z: scale
  f32v4 color; // x, y, z, w: color
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_graphic_data(0) readonly buffer Mesh { VertexPacked[] u_vertices; };
bind_instance_data(0) readonly uniform Instance { ShapeInstanceData[c_maxInstances] u_instances; };

bind_internal(0) out f32v4 out_color;

void main() {
  const Vertex vert = vert_unpack(u_vertices[in_vertexIndex]);

  const f32v3 instancePos   = u_instances[in_instanceIndex].pos.xyz;
  const f32v4 instanceQuat  = u_instances[in_instanceIndex].rot;
  const f32v3 instanceScale = u_instances[in_instanceIndex].scale.xyz;
  const f32v4 instanceColor = u_instances[in_instanceIndex].color;

  const f32v3 worldPos = quat_rotate(instanceQuat, vert.position * instanceScale) + instancePos;

  out_vertexPosition = u_global.viewProj * f32v4(worldPos, 1);
  out_color          = instanceColor;
}
