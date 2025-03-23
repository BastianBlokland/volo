#pragma once
#include "asset.h"
#include "ecs_module.h"

typedef enum {
  VfxAtlasType_Sprite,
  VfxAtlasType_StampColor,
  VfxAtlasType_StampNormal,
  VfxAtlasType_StampEmissive,

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
