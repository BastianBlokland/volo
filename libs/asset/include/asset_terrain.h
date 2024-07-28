#pragma once
#include "data_registry.h"
#include "ecs_entity.h"
#include "ecs_module.h"

// Forward declare from 'core_dynstring.h'.
typedef struct sDynArray DynString;

/**
 * NOTE: Colors are srgb encoded.
 */
typedef struct {
  f32 r, g, b;
} AssetTerrainColor;

ecs_comp_extern_public(AssetTerrainComp) {
  String      graphicId, heightmapId;
  EcsEntityId graphic, heightmap;

  u32 size;
  u32 playSize;
  f32 heightMax;

  AssetTerrainColor minimapColorLow, minimapColorHigh;
};

extern DataMeta g_assetTerrainDataDef;

void asset_terrain_jsonschema_write(DynString*);
