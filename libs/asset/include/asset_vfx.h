#pragma once
#include "core_time.h"
#include "ecs_module.h"
#include "geo_color.h"
#include "geo_quat.h"
#include "geo_vector.h"

#define asset_vfx_max_emitters 6

typedef enum {
  AssetVfxBlend_None,
  AssetVfxBlend_Alpha,
  AssetVfxBlend_Additive,
  AssetVfxBlend_AdditiveDouble,
} AssetVfxBlend;

typedef struct {
  GeoVector     position;
  GeoQuat       rotation;
  GeoColor      color;
  TimeDuration  fadeInTime, fadeOutTime;
  StringHash    atlasEntry;
  f32           sizeX, sizeY;
  TimeDuration  scaleInTime, scaleOutTime;
  AssetVfxBlend blend;
  u32           count;
  TimeDuration  interval, lifetime;
} AssetVfxEmitter;

ecs_comp_extern_public(AssetVfxComp) {
  u32             emitterCount;
  AssetVfxEmitter emitters[asset_vfx_max_emitters];
};
