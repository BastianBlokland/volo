#pragma once
#include "core_time.h"
#include "ecs_module.h"
#include "geo_color.h"
#include "geo_quat.h"
#include "geo_vector.h"

// Forward declare from 'core_dynstring.h'.
typedef struct sDynArray DynString;

#define asset_vfx_max_emitters 5

typedef enum {
  AssetVfxSpace_Local,
  AssetVfxSpace_World,
} AssetVfxSpace;

typedef enum {
  AssetVfxBlend_None,
  AssetVfxBlend_Alpha,
  AssetVfxBlend_Additive,
} AssetVfxBlend;

typedef enum {
  AssetVfxFacing_Local,
  AssetVfxFacing_BillboardSphere,
  AssetVfxFacing_BillboardCylinder,
} AssetVfxFacing;

typedef struct {
  GeoColor       color;
  StringHash     atlasEntry;
  AssetVfxBlend  blend : 8;
  AssetVfxFacing facing : 8;
  u16            flipbookCount;
  f32            flipbookTimeInv; // 1.0 / timeInSeconds.
  f32            sizeX, sizeY;
  f32            fadeInTimeInv, fadeOutTimeInv;   // 1.0 / timeInSeconds.
  f32            scaleInTimeInv, scaleOutTimeInv; // 1.0 / timeInSeconds.
  bool           geometryFade;                    // Aka 'soft particles'.
  bool           shadowCaster;
  bool           distortion; // Draw in the distortion pass instead of the forward pass.
} AssetVfxSprite;

typedef struct {
  GeoColor radiance;
  f32      fadeInTimeInv, fadeOutTimeInv; // 1.0 / timeInSeconds.
  f32      radius;
  f32      turbulenceFrequency; // Optional random scale turbulence.
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
  f32                   friction;
  AssetVfxSpace         space;
  AssetVfxSprite        sprite;
  AssetVfxLight         light;
  AssetVfxRangeScalar   speed;
  f32                   expandForce;
  u16                   count;
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

void asset_vfx_jsonschema_write(DynString*);
