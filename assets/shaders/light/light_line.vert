#include "binding.glsl"
#include "global.glsl"
#include "instance.glsl"
#include "vertex.glsl"

struct LightLineData {
  f32v4 posA, posB;        // x, y, z: position, w: unused
  f32v4 radianceAndRadius; // x, y, z: radiance, w: radius
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_graphic_data(0) readonly buffer Mesh { VertexPacked[] u_vertices; };
bind_instance_data(0) readonly uniform Instance { LightLineData[c_maxInstances] u_instances; };

bind_internal(0) out flat f32v3 out_positionA;
bind_internal(1) out flat f32v3 out_positionB;
bind_internal(2) out flat f32v4 out_radianceAndRadiusInv;

void main() {
  const Vertex vert = vert_unpack(u_vertices[in_vertexIndex]);

  const f32v3 instancePosA     = u_instances[in_instanceIndex].posA.xyz;
  const f32v3 instancePosB     = u_instances[in_instanceIndex].posB.xyz;
  const f32v4 instanceRadiance = u_instances[in_instanceIndex].radianceAndRadius.rgb;
  const f32   instanceRadius   = u_instances[in_instanceIndex].radianceAndRadius.w;

  const f32v3 worldPos = vert.position * instanceScale + instancePos;

  out_vertexPosition       = u_global.viewProj * f32v4(worldPos, 1);
  out_positionA            = instancePosA;
  out_positionB            = instancePosB;
  out_radianceAndRadiusInv = f32v4(instanceRadianceAndRadius.rgb, 1.0 / instanceRadius);
}
