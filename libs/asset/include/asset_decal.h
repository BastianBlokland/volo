#pragma once
#include "core_time.h"
#include "data_registry.h"
#include "ecs_module.h"

typedef enum {
  AssetDecalAxis_LocalY,
  AssetDecalAxis_LocalZ,
  AssetDecalAxis_WorldY,
} AssetDecalAxis;

typedef enum {
  AssetDecalNormal_GBuffer,        // The current gbuffer normal.
  AssetDecalNormal_DepthBuffer,    // Flat normals computed from the depth-buffer.
  AssetDecalNormal_DecalTransform, // The decal's own normal.
} AssetDecalNormal;

typedef enum {
  AssetDecalMask_Geometry = 1 << 0,
  AssetDecalMask_Terrain  = 1 << 1,
  AssetDecalMask_Unit     = 1 << 2,
} AssetDecalMask;

typedef enum {
  AssetDecalFlags_Trail                = 1 << 0,
  AssetDecalFlags_OutputColor          = 1 << 1, // Enable modifying the gbuffer color.
  AssetDecalFlags_FadeUsingDepthNormal = 1 << 2, // Fade using depth-buffer instead of gbuffer nrm.
  AssetDecalFlags_RandomRotation       = 1 << 3,
  AssetDecalFlags_SnapToTerrain        = 1 << 4,
} AssetDecalFlags;

ecs_comp_extern_public(AssetDecalComp) {
  StringHash       atlasColorEntry;
  StringHash       atlasNormalEntry; // Optional, 0 if unused.
  AssetDecalAxis   projectionAxis : 8;
  AssetDecalNormal baseNormal : 8; // Base normal where the normal-map is optionally applied on top.
  AssetDecalFlags  flags : 8;
  AssetDecalMask   excludeMask : 8;
  f32              spacing;
  f32              roughness;
  f32              alphaMin, alphaMax;
  f32              width, height;
  f32              thickness;
  f32              scaleMin, scaleMax;
  f32              fadeInTimeInv, fadeOutTimeInv; // 1.0f / timeInSeconds.
};

extern DataMeta g_assetDecalDefMeta;
