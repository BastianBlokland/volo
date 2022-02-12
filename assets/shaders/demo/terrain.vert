#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "instance.glsl"
#include "quat.glsl"
#include "vertex.glsl"

bind_spec(0) const f32 s_heightMapScale = 0.5;

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_graphic_data(0) readonly buffer Mesh { VertexPacked[] u_vertices; };
bind_instance_data(0) readonly uniform Instance { InstanceData[c_maxInstances] u_instances; };

bind_graphic(1) uniform sampler2D u_texHeightMap;

bind_internal(0) out flat f32v4 out_worldRotation;
bind_internal(1) out f32v2 out_texcoord;

f32 heightmap_sample(const f32v2 uv, const f32 scale) {
  return texture(u_texHeightMap, uv).r * scale;
}

void main() {
  const Vertex vert = vert_unpack(u_vertices[in_vertexIndex]);

  const f32   localHeight = heightmap_sample(vert.texcoord, s_heightMapScale);
  const f32v3 localPos    = f32v3(vert.position.x, vert.position.y + localHeight, vert.position.z);

  const f32v3 instancePos   = u_instances[in_instanceIndex].posAndScale.xyz;
  const f32   instanceScale = u_instances[in_instanceIndex].posAndScale.w;
  const f32v4 instanceQuat  = u_instances[in_instanceIndex].rot;

  const f32v3 worldPos = quat_rotate(instanceQuat, localPos * instanceScale) + instancePos;

  out_vertexPosition = u_global.viewProj * f32v4(worldPos, 1);
  out_worldRotation  = instanceQuat;
  out_texcoord       = vert.texcoord;
}
