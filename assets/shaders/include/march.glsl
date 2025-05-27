#ifndef INCLUDE_MARCH
#define INCLUDE_MARCH

#include "global.glsl"
#include "types.glsl"

#define MARCH_STEPS 32

bool march_test(const GlobalData data, const sampler2D depthTex, const f32v3 from, const f32v3 to) {
  const f32v3 rayStart  = world_to_view_pos(data, from);
  const f32v3 rayEnd    = world_to_view_pos(data, to);
  const f32v3 rayDelta  = rayEnd - rayStart;
  const f32   rayLength = length(rayDelta);
  if (rayLength < 0.001) {
    return false;
  }
  const f32v3 rayDir  = rayDelta / rayLength;
  const f32v3 rayStep = rayDir * (min(1, rayLength) / MARCH_STEPS);

  f32v3 pos = rayStart;
  for (u32 i = 0; i != MARCH_STEPS; ++i) {
    pos += rayStep;

    const f32v3 clipPos = view_to_clip_pos(data, pos);
    const f32v2 uv      = clipPos.xy * 0.5 + 0.5;

    if (uv.x >= 0 && uv.x <= 1 && uv.y >= 0 && uv.y <= 1) {
      const f32 depth       = texture(depthTex, uv).r;
      const f32 depthLinear = clip_to_view_depth(data, f32v3(clipPos.xy, depth));
      const f32 depthDelta  = pos.z - depthLinear;

      if (depthDelta > 0 && depthDelta < 2) {
        return true;
      }
    }
  }

  return false;
}

#define SHADOW_CONTACT_DIST 0.15
#define SHADOW_CONTACT_STEPS 16
#define SHADOW_CONTACT_MAX_DEPTH 0.05

bool shadow_contact(const GlobalData data, const sampler2D depthTex, const f32v3 pos, const f32v3 dir) {
  const f32v3 rayStart  = world_to_view_pos(data, pos);
  const f32v3 rayDir    = world_to_view_dir(data, dir);
  const f32v3 rayStep   = rayDir * (SHADOW_CONTACT_DIST / SHADOW_CONTACT_STEPS);

  f32v3 rayPos = rayStart;
  for (u32 i = 0; i != SHADOW_CONTACT_STEPS; ++i) {
    rayPos += rayStep;

    const f32v3 clipPos = view_to_clip_pos(data, rayPos);
    const f32v2 uv      = clipPos.xy * 0.5 + 0.5;

    if (uv.x >= 0 && uv.x <= 1 && uv.y >= 0 && uv.y <= 1) {
      const f32 depth       = texture(depthTex, uv).r;
      const f32 depthLinear = clip_to_view_depth(data, f32v3(clipPos.xy, depth));
      const f32 depthDelta  = rayPos.z - depthLinear;

      if (depthDelta > 0 && depthDelta < SHADOW_CONTACT_MAX_DEPTH) {
        return true;
      }
    }
  }

  return false;
}

#endif // INCLUDE_MARCH
