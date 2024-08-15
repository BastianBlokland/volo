#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

// Forward declare from 'asset_atlas.h'.
ecs_comp_extern(AssetAtlasComp);

typedef enum {
  VfxAtlasType_Sprite,
  VfxAtlasType_StampColor,
  VfxAtlasType_StampNormal,

  VfxAtlasType_Count,
} VfxAtlasType;

ecs_comp_extern(VfxAtlasManagerComp);

EcsEntityId vfx_atlas_entity(const VfxAtlasManagerComp*, VfxAtlasType);

typedef struct {
  ALIGNAS(16)
  f32 atlasEntriesPerDim;
  f32 atlasEntrySize;
  f32 atlasEntrySizeMinusPadding;
  f32 atlasEntryPadding;
} VfxAtlasDrawData;

ASSERT(sizeof(VfxAtlasDrawData) == 16, "Size needs to match the size defined in glsl");

VfxAtlasDrawData vfx_atlas_draw_data(const AssetAtlasComp*);
