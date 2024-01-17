#ifndef INCLUDE_GLOBAL
#define INCLUDE_GLOBAL

#include "types.glsl"

struct GlobalData {
  f32m4 view, viewInv;
  f32m4 proj, projInv;
  f32m4 viewProj, viewProjInv;
  f32v4 camPosition; // x, y, z position
  f32v4 camRotation; // x, y, z, w quaternion
  f32v4 resolution;  // x: width, y: height, z: aspect ratio (width / height), w: unused.
  f32v4 time;        // x: time seconds, y: real-time seconds, z, w: unused.
};

f32v3 clip_to_view_pos(const GlobalData data, const f32v3 clipPos) {
  const f32v4 v = data.projInv * f32v4(clipPos, 1);
  return v.xyz / v.w;
}

f32 clip_to_view_depth(const GlobalData data, const f32v3 clipPos) {
  const f32v4 v = data.projInv * f32v4(clipPos, 1);
  return v.z / v.w;
}

f32v3 clip_to_world_pos(const GlobalData data, const f32v3 clipPos) {
  const f32v4 v = data.viewProjInv * f32v4(clipPos, 1);
  return v.xyz / v.w;
}

f32v3 view_to_clip_pos(const GlobalData data, const f32v3 viewPos) {
  const f32v4 v = data.proj * f32v4(viewPos, 1);
  return v.xyz / v.w;
}

f32v3 view_to_world_dir(const GlobalData data, const f32v3 viewDir) {
  return (data.viewInv * f32v4(viewDir, 0)).xyz;
}

f32v2 world_to_clip_pos(const GlobalData data, const f32v3 worldPos) {
  const f32v4 v = data.viewProj * f32v4(worldPos, 1);
  return v.xy / v.w;
}

f32v3 world_to_view_dir(const GlobalData data, const f32v3 worldDir) {
  return (data.view * f32v4(worldDir, 0)).xyz;
}

#endif // INCLUDE_GLOBAL
