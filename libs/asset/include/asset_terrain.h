#pragma once
#include "asset_ref.h"
#include "data_registry.h"
#include "ecs_module.h"
#include "geo_color.h"

ecs_comp_extern_public(AssetTerrainComp) {
  AssetRef graphic, heightmap;

  u32 size;
  u32 playSize;
  f32 heightMax;

  GeoColor minimapColorLow, minimapColorHigh; // Srgb encoded.
};

extern DataMeta g_assetTerrainDefMeta;

/**
 * Find all asset references in the given terrain.
 */
u32 asset_terrain_refs(const AssetTerrainComp*, EcsEntityId out[], u32 outMax);
