#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "instance.glsl"
#include "vertex.glsl"

struct LightPointData {
  f32v4 posScale;             // x, y, z: position, w: scale
  f32v4 radianceAndRadiusInv; // x, y, z: radiance, w: inverse radius (1.0 / radius)
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_graphic_data(0) readonly buffer Mesh { VertexPacked[] u_vertices; };
bind_instance_data(0) readonly uniform Instance { LightPointData[c_maxInstances] u_instances; };

bind_internal(0) out flat f32v3 out_position;
bind_internal(1) out flat f32v4 out_radianceAndRadiusInv;

void main() {
  const Vertex vert = vert_unpack(u_vertices[in_vertexIndex]);

  const f32v3 instancePos                  = u_instances[in_instanceIndex].posScale.xyz;
  const f32   instanceScale                = u_instances[in_instanceIndex].posScale.w;
  const f32v4 instanceRadianceAndRadiusInv = u_instances[in_instanceIndex].radianceAndRadiusInv;

  const f32v3 worldPos = vert.position * instanceScale + instancePos;

  out_vertexPosition       = u_global.viewProj * f32v4(worldPos, 1);
  out_position             = instancePos.xyz;
  out_radianceAndRadiusInv = instanceRadianceAndRadiusInv;
}
