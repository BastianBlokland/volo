#pragma once
#include "data_registry.h"
#include "ecs_module.h"
#include "geo_color.h"

ecs_comp_extern_public(AssetTerrainComp) {
  String      graphicId, heightmapId;
  EcsEntityId graphic, heightmap;

  u32 size;
  u32 playSize;
  f32 heightMax;

  GeoColor minimapColorLow, minimapColorHigh; // Srgb encoded.
};

extern DataMeta g_assetTerrainDefMeta;
