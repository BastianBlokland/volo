#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

// Forward declare from 'core_dynstring.h'.
typedef struct sDynArray DynString;

typedef struct {
  f32 r, g, b;
} AssetTerrainColor;

ecs_comp_extern_public(AssetTerrainComp) {
  String      graphicId, heightmapId;
  EcsEntityId graphic, heightmap;

  f32 size, heightMax;

  AssetTerrainColor minimapColorLow, minimapColorHigh;
};

void asset_terrain_jsonschema_write(DynString*);
