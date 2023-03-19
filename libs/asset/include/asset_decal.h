#pragma once
#include "ecs_module.h"

typedef enum {
  AssetDecalNormal_GBuffer,        // The current gbuffer normal.
  AssetDecalNormal_DecalTransform, // The decals own normal.
} AssetDecalNormal;

ecs_comp_extern_public(AssetDecalComp) {
  StringHash       colorAtlasEntry;
  StringHash       normalAtlasEntry; // Optional, 0 if unused.
  AssetDecalNormal baseNormal; // Base normal where the normal-map is optionally applied on top.
  f32              roughness;
  f32              width, height;
  f32              thickness;
};
