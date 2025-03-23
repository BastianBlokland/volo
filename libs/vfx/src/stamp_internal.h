#pragma once
#include "asset.h"
#include "geo_quat.h"
#include "geo_vector.h"
#include "rend.h"
#include "vfx_warp.h"

/**
 * NOTE: Flag values are used in GLSL, update the GLSL side when changing these.
 */
typedef enum {
  VfxStamp_OutputColor           = 1 << 0, // Enable color output to the gbuffer.
  VfxStamp_OutputNormal          = 1 << 1, // Enable normal output to the gbuffer.
  VfxStamp_OutputEmissive        = 1 << 2, // Enable emissive output to the gbuffer.
  VfxStamp_GBufferBaseNormal     = 1 << 3, // Use the current gbuffer normal as the base normal.
  VfxStamp_DepthBufferBaseNormal = 1 << 4, // Compute the base normal from the depth buffer.
  VfxStamp_FadeUsingDepthNormal  = 1 << 5, // Angle fade using depth-buffer instead of gbuffer nrm.
} VfxStampFlags;

typedef struct {
  GeoVector     pos;
  GeoQuat       rot;
  u16           atlasColorIndex, atlasNormalIndex, atlasEmissiveIndex;
  VfxStampFlags flags : 8;
  u8            excludeTags;
  f32           alphaBegin, alphaEnd, roughness;
  f32           width, height, thickness;
  f32           texOffsetY, texScaleY;
  VfxWarpVec    warpScale;
  ALIGNAS(16) VfxWarpVec warpPoints[4];
} VfxStamp;

/**
 * Initialize a stamp render object.
 * NOTE: NOT thread-safe, should be called only once per frame.
 */
void vfx_stamp_init(
    RendObjectComp*,
    const AssetAtlasComp* atlasColor,
    const AssetAtlasComp* atlasNormal,
    const AssetAtlasComp* atlasEmissive);

/**
 * Output a stamp to the given render object.
 */
void vfx_stamp_output(RendObjectComp*, const VfxStamp*);
