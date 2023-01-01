#version 450
#extension GL_GOOGLE_include_directive : enable

#include "binding.glsl"
#include "global.glsl"
#include "instance.glsl"

struct LightDirData {
  f32v4 direction;      // x, y, z: direction, w: unused,
  f32v4 radianceFlags;  // x, y, z: radiance, w: flags,
  f32m4 shadowViewProj; // ShadowMap view-projection matrix.
};

bind_global_data(0) readonly uniform Global { GlobalData u_global; };
bind_instance_data(0) readonly uniform Instance { LightDirData[c_maxInstances] u_instances; };

bind_internal(0) out f32v2 out_texcoord;
bind_internal(1) out flat f32v3 out_direction;
bind_internal(2) out flat f32v4 out_radianceFlags;
bind_internal(3) out flat f32m4 out_shadowViewProj;

void main() {
  const f32v3 instanceDirectional   = u_instances[in_instanceIndex].direction.xyz;
  const f32v4 instanceRadianceFlags = u_instances[in_instanceIndex].radianceFlags;
  const f32m4 shadowViewProj        = u_instances[in_instanceIndex].shadowViewProj;

  /**
   * Fullscreen triangle at infinite depth.
   * More info: https://www.saschawillems.de/?page_id=2122
   */
  out_texcoord       = f32v2((in_vertexIndex << 1) & 2, in_vertexIndex & 2);
  out_vertexPosition = f32v4(out_texcoord * 2.0 - 1.0, 0.0, 1.0);
  out_direction      = instanceDirectional;
  out_radianceFlags  = instanceRadianceFlags;
  out_shadowViewProj = shadowViewProj;
}
