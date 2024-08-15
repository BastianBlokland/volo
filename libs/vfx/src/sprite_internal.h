#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_color.h"
#include "geo_quat.h"
#include "geo_vector.h"

// Forward declare from 'rend_draw.h'.
ecs_comp_extern(RendDrawComp);

// Forward declare from 'asset_atlas.h'.
ecs_comp_extern(AssetAtlasComp);

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
 * Initialize a sprite draw.
 * NOTE: NOT thread-safe, should be called only once per frame.
 */
void vfx_sprite_init(RendDrawComp*, const AssetAtlasComp*);

/**
 * Output a sprite to the given draw.
 */
void vfx_sprite_output(RendDrawComp*, const VfxSprite*);
