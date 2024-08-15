#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_quat.h"
#include "geo_vector.h"
#include "vfx_warp.h"

// Forward declare from 'rend_draw.h'.
ecs_comp_extern(RendDrawComp);

// Forward declare from 'asset_atlas.h'.
ecs_comp_extern(AssetAtlasComp);

/**
 * NOTE: Flag values are used in GLSL, update the GLSL side when changing these.
 */
typedef enum {
  VfxStamp_OutputColor           = 1 << 0, // Enable color output to the gbuffer.
  VfxStamp_OutputNormal          = 1 << 1, // Enable normal output to the gbuffer.
  VfxStamp_GBufferBaseNormal     = 1 << 2, // Use the current gbuffer normal as the base normal.
  VfxStamp_DepthBufferBaseNormal = 1 << 3, // Compute the base normal from the depth buffer.
  VfxStamp_FadeUsingDepthNormal  = 1 << 4, // Angle fade using depth-buffer instead of gbuffer nrm.
} VfxStampFlags;

typedef struct {
  GeoVector     pos;
  GeoQuat       rot;
  u16           atlasColorIndex, atlasNormalIndex;
  VfxStampFlags flags : 8;
  u8            excludeTags;
  f32           alphaBegin, alphaEnd, roughness;
  f32           width, height, thickness;
  f32           texOffsetY, texScaleY;
  VfxWarpVec    warpScale;
  ALIGNAS(16) VfxWarpVec warpPoints[4];
} VfxStamp;

/**
 * Initialize a stamp draw.
 * NOTE: NOT thread-safe, should be called only once per frame.
 */
void vfx_stamp_init(
    RendDrawComp*, const AssetAtlasComp* atlasColor, const AssetAtlasComp* atlasNormal);

/**
 * Output a stamp to the given draw.
 */
void vfx_stamp_output(RendDrawComp*, const VfxStamp*);
