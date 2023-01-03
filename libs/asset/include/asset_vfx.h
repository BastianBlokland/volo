#pragma once
#include "core_time.h"
#include "ecs_module.h"
#include "geo_color.h"
#include "geo_quat.h"
#include "geo_vector.h"

#define asset_vfx_max_emitters 4

typedef enum {
  AssetVfxSpace_Local,
  AssetVfxSpace_World,
} AssetVfxSpace;

typedef enum {
  AssetVfxBlend_None,
  AssetVfxBlend_Alpha,
  AssetVfxBlend_AlphaDouble,
  AssetVfxBlend_AlphaQuad,
  AssetVfxBlend_Additive,
  AssetVfxBlend_AdditiveDouble,
  AssetVfxBlend_AdditiveQuad,
} AssetVfxBlend;

typedef enum {
  AssetVfxFacing_Local,
  AssetVfxFacing_BillboardSphere,
  AssetVfxFacing_BillboardCylinder,
} AssetVfxFacing;

typedef struct {
  StringHash     atlasEntry;
  GeoColor       color;
  AssetVfxBlend  blend : 8;
  AssetVfxFacing facing : 8;
  u16            flipbookCount;
  TimeDuration   flipbookTime;
  f32            sizeX, sizeY;
  TimeDuration   fadeInTime, fadeOutTime;
  TimeDuration   scaleInTime, scaleOutTime;
  bool           geometryFade; // Aka 'soft particles'.
} AssetVfxSprite;

typedef struct {
  GeoColor     radiance;
  f32          attenuationLinear, attenuationQuad;
  TimeDuration fadeInTime, fadeOutTime;
} AssetVfxLight;

typedef struct {
  f32       angle;
  f32       radius;
  GeoVector position;
  GeoQuat   rotation;
} AssetVfxCone;

typedef struct {
  f32 min, max;
} AssetVfxRangeScalar;

typedef struct {
  TimeDuration min, max;
} AssetVfxRangeDuration;

typedef struct {
  GeoQuat   base;
  GeoVector randomEulerAngles;
} AssetVfxRangeRotation;

typedef struct {
  AssetVfxCone          cone;
  GeoVector             force;
  AssetVfxSpace         space;
  AssetVfxSprite        sprite;
  AssetVfxLight         light;
  AssetVfxRangeScalar   speed;
  f32                   expandForce;
  u32                   count;
  TimeDuration          interval;
  AssetVfxRangeScalar   scale;
  AssetVfxRangeDuration lifetime;
  AssetVfxRangeRotation rotation;
} AssetVfxEmitter;

typedef enum {
  AssetVfx_IgnoreTransformRotation = 1 << 0,
} AssetVfxFlags;

ecs_comp_extern_public(AssetVfxComp) {
  AssetVfxFlags    flags;
  u32              emitterCount;
  AssetVfxEmitter* emitters;
};
