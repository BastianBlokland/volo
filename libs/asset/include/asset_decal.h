#pragma once
#include "core_time.h"
#include "ecs_module.h"

typedef enum {
  AssetDecalAxis_LocalY,
  AssetDecalAxis_LocalZ,
  AssetDecalAxis_WorldY,
} AssetDecalAxis;

typedef enum {
  AssetDecalNormal_GBuffer,        // The current gbuffer normal.
  AssetDecalNormal_DepthBuffer,    // Flat normals computed from the depth-buffer.
  AssetDecalNormal_DecalTransform, // The decals own normal.
} AssetDecalNormal;

ecs_comp_extern_public(AssetDecalComp) {
  AssetDecalAxis   projectionAxis;
  StringHash       colorAtlasEntry;
  StringHash       normalAtlasEntry; // Optional, 0 if unused.
  AssetDecalNormal baseNormal; // Base normal where the normal-map is optionally applied on top.
  bool             fadeUsingDepthNormal; // Angle fade using depth-buffer instead of gbuffer normal.
  bool             noColorOutput;        // Disable modifying the gbuffer color.
  bool             randomRotation;
  f32              roughness;
  f32              alpha;
  f32              width, height;
  f32              thickness;
  TimeDuration     fadeInTime, fadeOutTime;
};
