#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "instance.glsl"
#include "vertex.glsl"

struct FogVisionData {
  f32v4 data1; // x, y, z: position, w: radius
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_graphic_data(0) readonly buffer Mesh { VertexPacked[] u_vertices; };
bind_instance_data(0) readonly uniform Instance { FogVisionData[c_maxInstances] u_instances; };

void main() {
  const Vertex vert = vert_unpack(u_vertices[in_vertexIndex]);

  const f32v3 instancePos    = u_instances[in_instanceIndex].data1.xyz;
  const f32   instanceRadius = u_instances[in_instanceIndex].data1.w;

  const f32v3 worldPos = vert.position * (instanceRadius * 2) + instancePos;

  out_vertexPosition = u_global.viewProj * f32v4(worldPos, 1);
}
