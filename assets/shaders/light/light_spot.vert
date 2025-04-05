#include "binding.glsl"
#include "global.glsl"
#include "instance.glsl"
#include "math.glsl"
#include "vertex.glsl"

struct LightSpotData {
  f32v4 posAndLength;      // x, y, z: position, w: length.
  f32v4 dirAndAngleCos;    // x, y, z: direction, w: cos(angle).
  f32v4 radianceAndRadius; // x, y, z: radiance, w: radius.
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_graphic_data(0) readonly buffer Mesh { VertexPacked[] u_vertices; };
bind_instance_data(0) readonly uniform Instance { LightSpotData[c_maxInstances] u_instances; };

bind_internal(0) out flat f32v3 out_position;
bind_internal(1) out flat f32v4 out_directionAndAngleCos;
bind_internal(2) out flat f32v4 out_radianceAndLengthInv;

void main() {
  const Vertex vert = vert_unpack(u_vertices[in_vertexIndex]);

  const f32v3 instancePos      = u_instances[in_instanceIndex].posAndLength.xyz;
  const f32   instanceLength   = u_instances[in_instanceIndex].posAndLength.w;
  const f32v3 instanceDir      = u_instances[in_instanceIndex].dirAndAngleCos.xyz;
  const f32   instanceAngleCos = u_instances[in_instanceIndex].dirAndAngleCos.w;
  const f32v3 instanceRadiance = u_instances[in_instanceIndex].radianceAndRadius.rgb;
  const f32   instanceRadius   = u_instances[in_instanceIndex].radianceAndRadius.w;

  const f32m3 rot      = math_rotate_look_f32m3(instanceDir, f32v3(0, 1, 0));
  const f32v3 scale    = f32v3(instanceRadius, instanceRadius, instanceLength);
  const f32v3 worldPos = rot * (vert.position * scale) + instancePos;

  out_vertexPosition       = u_global.viewProj * f32v4(worldPos, 1);
  out_position             = instancePos;
  out_directionAndAngleCos = f32v4(instanceDir, instanceAngleCos);
  out_radianceAndLengthInv = f32v4(instanceRadiance, 1.0 / instanceLength);
}
