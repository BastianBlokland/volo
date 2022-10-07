#pragma once
#include "ecs_module.h"
#include "geo_color.h"
#include "geo_quat.h"
#include "geo_vector.h"

#define asset_vfx_max_emitters 8

typedef struct {
  StringHash atlasEntry;
  GeoVector  position;
  GeoQuat    rotation;
  f32        sizeX, sizeY;
  GeoColor   color;
} AssetVfxEmitter;

ecs_comp_extern_public(AssetVfxComp) {
  u32             emitterCount;
  AssetVfxEmitter emitters[asset_vfx_max_emitters];
};
