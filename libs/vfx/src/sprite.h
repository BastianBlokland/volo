#pragma once
#include "asset/forward.h"
#include "geo/color.h"
#include "geo/quat.h"
#include "geo/vector.h"
#include "rend/forward.h"

/**
 * NOTE: Flag values are used in GLSL, update the GLSL side when changing these.
 */
typedef enum {
  VfxSprite_GeometryFade      = 1 << 0, // Aka 'soft particles'.
  VfxSprite_BillboardSphere   = 1 << 1,
  VfxSprite_BillboardCylinder = 1 << 2,
  VfxSprite_Billboard         = VfxSprite_BillboardSphere | VfxSprite_BillboardCylinder,
  VfxSprite_ShadowCaster      = 1 << 3,
} VfxSpriteFlags;

typedef struct {
  GeoVector      position;
  GeoQuat        rotation;
  VfxSpriteFlags flags : 16;
  u16            atlasIndex;
  f32            sizeX, sizeY;
  f32            opacity;
  GeoColor       color;
} VfxSprite;

/**
 * Initialize a sprite render object.
 * NOTE: NOT thread-safe, should be called only once per frame.
 */
void vfx_sprite_init(RendObjectComp*, const AssetAtlasComp*);

/**
 * Output a sprite to the given render object.
 */
void vfx_sprite_output(RendObjectComp*, const VfxSprite*);
