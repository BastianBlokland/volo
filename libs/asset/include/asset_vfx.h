#pragma once
#include "core_time.h"
#include "ecs_module.h"
#include "geo_color.h"
#include "geo_quat.h"
#include "geo_vector.h"

#define asset_vfx_max_emitters 5

typedef enum {
  AssetVfxBlend_None,
  AssetVfxBlend_Alpha,
  AssetVfxBlend_Additive,
  AssetVfxBlend_AdditiveDouble,
  AssetVfxBlend_AdditiveQuad,
} AssetVfxBlend;

typedef enum {
  AssetVfxFacing_World,
  AssetVfxFacing_BillboardSphere,
  AssetVfxFacing_BillboardCylinder,
} AssetVfxFacing;

typedef struct {
  f32       angle;
  GeoVector position;
  GeoQuat   rotation;
} AssetVfxCone;

typedef struct {
  f32 min, max;
} AssetVfxRangeScalar;

typedef struct {
  AssetVfxCone        cone;
  GeoVector           position;
  GeoQuat             rotation;
  GeoColor            color;
  TimeDuration        fadeInTime, fadeOutTime;
  StringHash          atlasEntry;
  u32                 flipbookCount;
  TimeDuration        flipbookTime;
  f32                 sizeX, sizeY;
  TimeDuration        scaleInTime, scaleOutTime;
  AssetVfxBlend       blend;
  AssetVfxFacing      facing;
  AssetVfxRangeScalar speed;
  u32                 count;
  TimeDuration        interval, lifetime;
} AssetVfxEmitter;

ecs_comp_extern_public(AssetVfxComp) {
  u32             emitterCount;
  AssetVfxEmitter emitters[asset_vfx_max_emitters];
};
