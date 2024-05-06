#include "asset_atlas.h"
#include "asset_decal.h"
#include "asset_manager.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "core_rng.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "rend_draw.h"
#include "scene_lifetime.h"
#include "scene_set.h"
#include "scene_tag.h"
#include "scene_terrain.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "scene_vfx.h"
#include "scene_visibility.h"
#include "vfx_register.h"
#include "vfx_warp.h"

#include "atlas_internal.h"
#include "draw_internal.h"

#define vfx_decal_max_create_per_tick 100
#define vfx_decal_max_asset_requests 4
#define vfx_decal_trail_history_count 12
#define vfx_decal_trail_spline_points (vfx_decal_trail_history_count + 3)
#define vfx_decal_trail_seg_min_length 0.1f
#define vfx_decal_trail_seg_count_max 52
#define vfx_decal_trail_step 0.25f

typedef struct {
  VfxAtlasDrawData atlasColor, atlasNormal;
} VfxDecalMetaData;

ASSERT(sizeof(VfxDecalMetaData) == 32, "Size needs to match the size defined in glsl");

/**
 * NOTE: Flag values are used in GLSL, update the GLSL side when changing these.
 */
typedef enum {
  VfxDecal_OutputColor           = 1 << 0, // Enable color output to the gbuffer.
  VfxDecal_OutputNormal          = 1 << 1, // Enable normal output to the gbuffer.
  VfxDecal_GBufferBaseNormal     = 1 << 2, // Use the current gbuffer normal as the base normal.
  VfxDecal_DepthBufferBaseNormal = 1 << 3, // Compute the base normal from the depth buffer.
  VfxDecal_FadeUsingDepthNormal  = 1 << 4, // Angle fade using depth-buffer instead of gbuffer nrm.
} VfxDecalFlags;

typedef struct {
  ALIGNAS(16)
  f32 data1[4]; // xyz: position, w: 16b flags, 16b excludeTags.
  f16 data2[4]; // xyzw: rotation quaternion.
  f16 data3[4]; // xyz: scale, w: roughness.
  f16 data4[4]; // x: atlasColorIndex, y: atlasNormalIndex, z: alphaBegin, w: alphaEnd.
  f16 data5[4]; // xy: warpScale, z: texOffsetY, w: texScaleY.
  f16 warpPoints[4][2];
} VfxDecalData;

ASSERT(sizeof(VfxDecalData) == 64, "Size needs to match the size defined in glsl");

typedef enum {
  VfxLoad_Acquired  = 1 << 0,
  VfxLoad_Unloading = 1 << 1,
} VfxLoadFlags;

ecs_comp_define(VfxDecalAnyComp);

ecs_comp_define(VfxDecalSingleComp) {
  u16            atlasColorIndex, atlasNormalIndex;
  VfxDecalFlags  decalFlags : 8;
  AssetDecalAxis axis : 8;
  u8             excludeTags; // First 8 entries of SceneTags are supported.
  bool           snapToTerrain;
  f32            angle;
  f32            roughness, alpha;
  f32            fadeInSec, fadeOutSec;
  f32            width, height, thickness;
  TimeDuration   creationTime;
};

typedef enum {
  VfxTrailFlags_HistoryReset = 1 << 0,
} VfxTrailFlags;

ecs_comp_define(VfxDecalTrailComp) {
  u16            atlasColorIndex, atlasNormalIndex;
  VfxDecalFlags  decalFlags : 8;
  VfxTrailFlags  trailFlags : 8;
  AssetDecalAxis axis : 8;
  bool           snapToTerrain;
  u8             excludeTags; // First 8 entries of SceneTags are supported.
  f32            roughness, alpha;
  f32            fadeInSec, fadeOutSec;
  f32            width, height, thickness;
  TimeDuration   creationTime;
  f32            pointSpacing, nextPointFrac;
  u32            historyNewest, historyCountTotal;
  GeoVector      history[vfx_decal_trail_history_count];
  f32            historyAlpha[vfx_decal_trail_history_count];
};

ecs_comp_define(VfxDecalAssetComp) { VfxLoadFlags loadFlags; };

static void ecs_combine_decal_asset(void* dataA, void* dataB) {
  VfxDecalAssetComp* compA = dataA;
  VfxDecalAssetComp* compB = dataB;
  compA->loadFlags |= compB->loadFlags;
}

ecs_view_define(GlobalView) {
  ecs_access_read(SceneTerrainComp);
  ecs_access_read(SceneTimeComp);
  ecs_access_read(SceneVisibilityEnvComp);
  ecs_access_read(VfxAtlasManagerComp);
  ecs_access_read(VfxDrawManagerComp);
}

ecs_view_define(AtlasView) { ecs_access_read(AssetAtlasComp); }

ecs_view_define(DecalDrawView) { ecs_access_write(RendDrawComp); }

ecs_view_define(DecalAnyView) {
  ecs_access_read(SceneVfxDecalComp);
  ecs_access_with(VfxDecalAnyComp);
}

static const AssetAtlasComp*
vfx_atlas(EcsWorld* world, const VfxAtlasManagerComp* manager, const VfxAtlasType type) {
  const EcsEntityId atlasEntity = vfx_atlas_entity(manager, type);
  EcsIterator*      itr = ecs_view_maybe_at(ecs_world_view_t(world, AtlasView), atlasEntity);
  return LIKELY(itr) ? ecs_view_read_t(itr, AssetAtlasComp) : null;
}

static void vfx_decal_reset_all(EcsWorld* world, const EcsEntityId asset) {
  EcsView* decalAnyView = ecs_world_view_t(world, DecalAnyView);
  for (EcsIterator* itr = ecs_view_itr(decalAnyView); ecs_view_walk(itr);) {
    if (ecs_view_read_t(itr, SceneVfxDecalComp)->asset == asset) {
      const EcsEntityId entity = ecs_view_entity(itr);
      ecs_world_remove_t(world, entity, VfxDecalAnyComp);
      ecs_utils_maybe_remove_t(world, entity, VfxDecalSingleComp);
      ecs_utils_maybe_remove_t(world, entity, VfxDecalTrailComp);
    }
  }
}

ecs_view_define(LoadView) { ecs_access_write(VfxDecalAssetComp); }

ecs_system_define(VfxDecalLoadSys) {
  for (EcsIterator* itr = ecs_view_itr(ecs_world_view_t(world, LoadView)); ecs_view_walk(itr);) {
    const EcsEntityId  entity     = ecs_view_entity(itr);
    VfxDecalAssetComp* request    = ecs_view_write_t(itr, VfxDecalAssetComp);
    const bool         isLoaded   = ecs_world_has_t(world, entity, AssetLoadedComp);
    const bool         isFailed   = ecs_world_has_t(world, entity, AssetFailedComp);
    const bool         hasChanged = ecs_world_has_t(world, entity, AssetChangedComp);

    if (request->loadFlags & VfxLoad_Acquired && (isLoaded || isFailed) && hasChanged) {
      asset_release(world, entity);
      request->loadFlags &= ~VfxLoad_Acquired;
      request->loadFlags |= VfxLoad_Unloading;
    }
    if (request->loadFlags & VfxLoad_Unloading && !isLoaded) {
      request->loadFlags &= ~VfxLoad_Unloading;
      vfx_decal_reset_all(world, entity);
    }
    if (!(request->loadFlags & (VfxLoad_Acquired | VfxLoad_Unloading))) {
      asset_acquire(world, entity);
      request->loadFlags |= VfxLoad_Acquired;
    }
  }
}

static bool vfx_decal_asset_valid(EcsWorld* world, const EcsEntityId assetEntity) {
  return ecs_world_exists(world, assetEntity) && ecs_world_has_t(world, assetEntity, AssetComp);
}

static bool vfx_decal_asset_request(EcsWorld* world, const EcsEntityId assetEntity) {
  if (!ecs_world_has_t(world, assetEntity, VfxDecalAssetComp)) {
    ecs_world_add_t(world, assetEntity, VfxDecalAssetComp);
    return true;
  }
  return false;
}

ecs_view_define(InitView) {
  ecs_access_read(SceneVfxDecalComp);
  ecs_access_without(VfxDecalAnyComp);
}

ecs_view_define(InitAssetView) {
  ecs_access_with(VfxDecalAssetComp);
  ecs_access_read(AssetDecalComp);
}

static VfxDecalFlags vfx_decal_flags(const AssetDecalComp* asset) {
  VfxDecalFlags flags = 0;
  if (asset->flags & AssetDecalFlags_OutputColor) {
    flags |= VfxDecal_OutputColor;
  }
  if (asset->atlasNormalEntry) {
    flags |= VfxDecal_OutputNormal;
  }
  switch (asset->baseNormal) {
  case AssetDecalNormal_GBuffer:
    flags |= VfxDecal_GBufferBaseNormal;
    break;
  case AssetDecalNormal_DepthBuffer:
    flags |= VfxDecal_DepthBufferBaseNormal;
    break;
  case AssetDecalNormal_DecalTransform:
    // DecalTransform as the base-normal is the default.
    break;
  }
  if (asset->flags & AssetDecalFlags_FadeUsingDepthNormal) {
    flags |= VfxDecal_FadeUsingDepthNormal;
  }
  return flags;
}

static u8 vfx_decal_mask_to_tags(const AssetDecalMask mask) {
  u8 excludeTags = 0;
  excludeTags |= mask & AssetDecalMask_Unit ? SceneTags_Unit : 0;
  excludeTags |= mask & AssetDecalMask_Geometry ? SceneTags_Geometry : 0;
  return excludeTags;
}

static void vfx_decal_create_single(
    EcsWorld*             world,
    const EcsEntityId     entity,
    const u16             atlasColorIndex,
    const u16             atlasNormalIndex,
    const AssetDecalComp* asset,
    const SceneTimeComp*  timeComp) {

  const f32  alpha          = rng_sample_range(g_rng, asset->alphaMin, asset->alphaMax);
  const f32  scale          = rng_sample_range(g_rng, asset->scaleMin, asset->scaleMax);
  const bool randomRotation = (asset->flags & AssetDecalFlags_RandomRotation) != 0;
  ecs_world_add_empty_t(world, entity, VfxDecalAnyComp);
  ecs_world_add_t(
      world,
      entity,
      VfxDecalSingleComp,
      .atlasColorIndex  = atlasColorIndex,
      .atlasNormalIndex = atlasNormalIndex,
      .decalFlags       = vfx_decal_flags(asset),
      .axis             = asset->projectionAxis,
      .excludeTags      = vfx_decal_mask_to_tags(asset->excludeMask),
      .snapToTerrain    = (asset->flags & AssetDecalFlags_SnapToTerrain) != 0,
      .angle            = randomRotation ? rng_sample_f32(g_rng) * math_pi_f32 * 2.0f : 0.0f,
      .roughness        = asset->roughness,
      .alpha            = alpha,
      .fadeInSec        = asset->fadeInTime ? asset->fadeInTime / (f32)time_second : -1.0f,
      .fadeOutSec       = asset->fadeOutTime ? asset->fadeOutTime / (f32)time_second : -1.0f,
      .creationTime     = timeComp->time,
      .width            = asset->width * scale,
      .height           = asset->height * scale,
      .thickness        = asset->thickness);
}

static void vfx_decal_create_trail(
    EcsWorld*             world,
    const EcsEntityId     entity,
    const u16             atlasColorIndex,
    const u16             atlasNormalIndex,
    const AssetDecalComp* asset,
    const SceneTimeComp*  timeComp) {

  const f32 alpha = rng_sample_range(g_rng, asset->alphaMin, asset->alphaMax);
  const f32 scale = rng_sample_range(g_rng, asset->scaleMin, asset->scaleMax);
  ecs_world_add_empty_t(world, entity, VfxDecalAnyComp);
  ecs_world_add_t(
      world,
      entity,
      VfxDecalTrailComp,
      .decalFlags       = vfx_decal_flags(asset),
      .trailFlags       = VfxTrailFlags_HistoryReset,
      .atlasColorIndex  = atlasColorIndex,
      .atlasNormalIndex = atlasNormalIndex,
      .axis             = asset->projectionAxis,
      .excludeTags      = vfx_decal_mask_to_tags(asset->excludeMask),
      .snapToTerrain    = (asset->flags & AssetDecalFlags_SnapToTerrain) != 0,
      .pointSpacing     = asset->spacing,
      .roughness        = asset->roughness,
      .alpha            = alpha,
      .fadeInSec        = asset->fadeInTime ? asset->fadeInTime / (f32)time_second : -1.0f,
      .fadeOutSec       = asset->fadeOutTime ? asset->fadeOutTime / (f32)time_second : -1.0f,
      .creationTime     = timeComp->time,
      .width            = asset->width * scale,
      .height           = asset->height * scale,
      .thickness        = asset->thickness);
}

ecs_system_define(VfxDecalInitSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp*       timeComp     = ecs_view_read_t(globalItr, SceneTimeComp);
  const VfxAtlasManagerComp* atlasManager = ecs_view_read_t(globalItr, VfxAtlasManagerComp);
  const AssetAtlasComp*      atlasColor   = vfx_atlas(world, atlasManager, VfxAtlasType_DecalColor);
  const AssetAtlasComp*      atlasNormal = vfx_atlas(world, atlasManager, VfxAtlasType_DecalNormal);
  if (!atlasColor || !atlasNormal) {
    return; // Atlas hasn't loaded yet.
  }

  EcsIterator* assetItr         = ecs_view_itr(ecs_world_view_t(world, InitAssetView));
  u32          numDecalCreate   = 0;
  u32          numAssetRequests = 0;

  EcsView* initView = ecs_world_view_t(world, InitView);
  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    const EcsEntityId        e     = ecs_view_entity(itr);
    const SceneVfxDecalComp* decal = ecs_view_read_t(itr, SceneVfxDecalComp);

    if (!ecs_view_maybe_jump(assetItr, decal->asset)) {
      if (UNLIKELY(!vfx_decal_asset_valid(world, decal->asset))) {
        log_e("Invalid decal asset entity");
        continue;
      } else if (UNLIKELY(ecs_world_has_t(world, decal->asset, AssetFailedComp))) {
        log_e("Failed to acquire decal asset");
        continue;
      } else if (UNLIKELY(ecs_world_has_t(world, decal->asset, AssetLoadedComp))) {
        log_e("Acquired asset was not a decal");
        continue;
      }
      if (++numAssetRequests < vfx_decal_max_asset_requests) {
        vfx_decal_asset_request(world, decal->asset);
      }
      continue;
    }
    const AssetDecalComp* asset           = ecs_view_read_t(assetItr, AssetDecalComp);
    u16                   atlasColorIndex = 0, atlasNormalIndex = 0;
    {
      const AssetAtlasEntry* entry = asset_atlas_lookup(atlasColor, asset->atlasColorEntry);
      if (UNLIKELY(!entry)) {
        log_e("Vfx decal color-atlas entry missing");
        continue;
      }
      atlasColorIndex = entry->atlasIndex;
    }
    if (asset->atlasNormalEntry) {
      const AssetAtlasEntry* entry = asset_atlas_lookup(atlasNormal, asset->atlasNormalEntry);
      if (UNLIKELY(!entry)) {
        log_e("Vfx decal normal-atlas entry missing");
        continue;
      }
      atlasNormalIndex = entry->atlasIndex;
    }
    if (asset->flags & AssetDecalFlags_Trail) {
      vfx_decal_create_trail(world, e, atlasColorIndex, atlasNormalIndex, asset, timeComp);
    } else {
      vfx_decal_create_single(world, e, atlasColorIndex, atlasNormalIndex, asset, timeComp);
    }

    if (++numDecalCreate == vfx_decal_max_create_per_tick) {
      break; // Throttle the maximum amount of decals to create per tick.
    }
  }
}

ecs_view_define(DeinitView) {
  ecs_access_with(VfxDecalAnyComp);
  ecs_access_without(SceneVfxDecalComp);
}

ecs_system_define(VfxDecalDeinitSys) {
  EcsView* deinitView = ecs_world_view_t(world, DeinitView);
  for (EcsIterator* itr = ecs_view_itr(deinitView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, VfxDecalAnyComp);
    ecs_utils_maybe_remove_t(world, entity, VfxDecalSingleComp);
    ecs_utils_maybe_remove_t(world, entity, VfxDecalTrailComp);
  }
}

static void vfx_decal_draw_init(
    RendDrawComp* draw, const AssetAtlasComp* atlasColor, const AssetAtlasComp* atlasNormal) {
  *rend_draw_set_data_t(draw, VfxDecalMetaData) = (VfxDecalMetaData){
      .atlasColor  = vfx_atlas_draw_data(atlasColor),
      .atlasNormal = vfx_atlas_draw_data(atlasNormal),
  };
}

static RendDrawComp*
vfx_draw_get(EcsWorld* world, const VfxDrawManagerComp* drawManager, const VfxDrawType type) {
  const EcsEntityId drawEntity = vfx_draw_entity(drawManager, type);
  return ecs_utils_write_t(world, DecalDrawView, drawEntity, RendDrawComp);
}

typedef struct {
  GeoVector     pos;
  GeoQuat       rot;
  u16           atlasColorIndex, atlasNormalIndex;
  VfxDecalFlags flags : 8;
  u8            excludeTags;
  f32           alphaBegin, alphaEnd, roughness;
  f32           width, height, thickness;
  f32           texOffsetY, texScaleY;
  VfxWarpVec    warpScale;
  VfxWarpVec    warpPoints[4];
} VfxDecalParams;

static void vfx_decal_draw_output(RendDrawComp* draw, const VfxDecalParams* params) {
  const GeoVector decalSize = geo_vector(params->width, params->height, params->thickness);
  const GeoVector warpScale = geo_vector(params->warpScale.x, params->warpScale.y, 1);

  const GeoBox box = geo_box_from_center(params->pos, geo_vector_mul_comps(decalSize, warpScale));
  const GeoBox bounds = geo_box_from_rotated(&box, params->rot);

  VfxDecalData* out = rend_draw_add_instance_t(draw, VfxDecalData, SceneTags_Vfx, bounds);
  mem_cpy(array_mem(out->data1), mem_create(params->pos.comps, sizeof(f32) * 3));
  out->data1[3] = bits_u32_as_f32((u32)params->flags | ((u32)params->excludeTags << 16));

  geo_quat_pack_f16(params->rot, out->data2);

  out->data3[0] = float_f32_to_f16(decalSize.x);
  out->data3[1] = float_f32_to_f16(decalSize.y);
  out->data3[2] = float_f32_to_f16(decalSize.z);
  out->data3[3] = float_f32_to_f16(params->roughness);

  diag_assert_msg(params->atlasColorIndex <= 1024, "Index not representable by 16 bit float");
  diag_assert_msg(params->atlasNormalIndex <= 1024, "Index not representable by 16 bit float");

  out->data4[0] = float_f32_to_f16((f32)params->atlasColorIndex);
  out->data4[1] = float_f32_to_f16((f32)params->atlasNormalIndex);
  out->data4[2] = float_f32_to_f16(params->alphaBegin);
  out->data4[3] = float_f32_to_f16(params->alphaEnd);

  out->data5[0] = float_f32_to_f16(warpScale.x);
  out->data5[1] = float_f32_to_f16(warpScale.y);
  out->data5[2] = float_f32_to_f16(params->texOffsetY);
  out->data5[3] = float_f32_to_f16(params->texScaleY);

  for (u32 i = 0; i != 4; ++i) {
    out->warpPoints[i][0] = float_f32_to_f16(params->warpPoints[i].x);
    out->warpPoints[i][1] = float_f32_to_f16(params->warpPoints[i].y);
  }
}

static GeoQuat vfx_decal_rotation(const GeoQuat rot, const AssetDecalAxis axis) {
  switch (axis) {
  case AssetDecalAxis_LocalY:
    return geo_quat_mul(rot, geo_quat_forward_to_up);
  case AssetDecalAxis_LocalZ:
    return rot;
  case AssetDecalAxis_WorldY:
    return geo_quat_forward_to_up;
  }
  UNREACHABLE
}

static f32 vfx_decal_fade_in(
    const SceneTimeComp* timeComp, const TimeDuration creationTime, const f32 fadeInSec) {
  if (fadeInSec > 0) {
    const f32 ageSec = (timeComp->time - creationTime) / (f32)time_second;
    return math_min(ageSec / fadeInSec, 1.0f);
  }
  return 1.0f;
}

static f32 vfx_decal_fade_out(const SceneLifetimeDurationComp* lifetime, const f32 fadeOutSec) {
  if (fadeOutSec > 0) {
    const f32 timeRemSec = lifetime ? lifetime->duration / (f32)time_second : f32_max;
    return math_min(timeRemSec / fadeOutSec, 1.0f);
  }
  return 1.0f;
}

ecs_view_define(UpdateSingleView) {
  ecs_access_maybe_read(SceneLifetimeDurationComp);
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneSetMemberComp);
  ecs_access_maybe_read(SceneVisibilityComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_read(SceneVfxDecalComp);
  ecs_access_read(VfxDecalSingleComp);
}

static void vfx_decal_single_update(
    const SceneTimeComp*          timeComp,
    const SceneTerrainComp*       terrainComp,
    const SceneVisibilityEnvComp* visEnv,
    RendDrawComp*                 drawNormal,
    RendDrawComp*                 drawDebug,
    EcsIterator*                  itr) {
  const VfxDecalSingleComp*        inst      = ecs_view_read_t(itr, VfxDecalSingleComp);
  const SceneTransformComp*        trans     = ecs_view_read_t(itr, SceneTransformComp);
  const SceneScaleComp*            scaleComp = ecs_view_read_t(itr, SceneScaleComp);
  const SceneSetMemberComp*        setMember = ecs_view_read_t(itr, SceneSetMemberComp);
  const SceneVfxDecalComp*         decal     = ecs_view_read_t(itr, SceneVfxDecalComp);
  const SceneLifetimeDurationComp* lifetime  = ecs_view_read_t(itr, SceneLifetimeDurationComp);

  const SceneVisibilityComp* visComp = ecs_view_read_t(itr, SceneVisibilityComp);
  if (visComp && !scene_visible_for_render(visEnv, visComp)) {
    return;
  }

  const bool debug = setMember && scene_set_member_contains(setMember, g_sceneSetSelected);

  GeoVector pos = trans->position;
  if (inst->snapToTerrain) {
    scene_terrain_snap(terrainComp, &pos);
  }

  const GeoQuat        rotRaw = vfx_decal_rotation(trans->rotation, inst->axis);
  const GeoQuat        rot    = geo_quat_mul(rotRaw, geo_quat_angle_axis(inst->angle, geo_forward));
  const f32            scale  = scaleComp ? scaleComp->scale : 1.0f;
  const f32            fadeIn = vfx_decal_fade_in(timeComp, inst->creationTime, inst->fadeInSec);
  const f32            fadeOut = vfx_decal_fade_out(lifetime, inst->fadeOutSec);
  const f32            alpha   = decal->alpha * inst->alpha * fadeIn * fadeOut;
  const VfxDecalParams params  = {
      .pos              = pos,
      .rot              = rot,
      .width            = inst->width * scale,
      .height           = inst->height * scale,
      .thickness        = inst->thickness,
      .flags            = inst->decalFlags,
      .excludeTags      = inst->excludeTags,
      .atlasColorIndex  = inst->atlasColorIndex,
      .atlasNormalIndex = inst->atlasNormalIndex,
      .alphaBegin       = alpha,
      .alphaEnd         = alpha,
      .roughness        = inst->roughness,
      .texOffsetY       = 0.0f,
      .texScaleY        = 1.0f,
      .warpScale        = {1.0f, 1.0f},
      .warpPoints       = {{0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}, {0.0f, 1.0f}},
  };

  vfx_decal_draw_output(drawNormal, &params);
  if (UNLIKELY(debug)) {
    vfx_decal_draw_output(drawDebug, &params);
  }
}

ecs_system_define(VfxDecalSingleUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp*       timeComp     = ecs_view_read_t(globalItr, SceneTimeComp);
  const SceneTerrainComp*    terrainComp  = ecs_view_read_t(globalItr, SceneTerrainComp);
  const VfxAtlasManagerComp* atlasManager = ecs_view_read_t(globalItr, VfxAtlasManagerComp);
  const AssetAtlasComp*      atlasColor   = vfx_atlas(world, atlasManager, VfxAtlasType_DecalColor);
  const AssetAtlasComp*      atlasNormal = vfx_atlas(world, atlasManager, VfxAtlasType_DecalNormal);
  if (!atlasColor || !atlasNormal) {
    return; // Atlas hasn't loaded yet.
  }

  const SceneVisibilityEnvComp* visEnv      = ecs_view_read_t(globalItr, SceneVisibilityEnvComp);
  const VfxDrawManagerComp*     drawManager = ecs_view_read_t(globalItr, VfxDrawManagerComp);

  RendDrawComp* drawNormal = vfx_draw_get(world, drawManager, VfxDrawType_DecalSingle);
  RendDrawComp* drawDebug  = vfx_draw_get(world, drawManager, VfxDrawType_DecalSingleDebug);

  vfx_decal_draw_init(drawNormal, atlasColor, atlasNormal);
  vfx_decal_draw_init(drawDebug, atlasColor, atlasNormal);

  EcsView* singleView = ecs_world_view_t(world, UpdateSingleView);
  for (EcsIterator* itr = ecs_view_itr(singleView); ecs_view_walk(itr);) {
    vfx_decal_single_update(timeComp, terrainComp, visEnv, drawNormal, drawDebug, itr);
  }
}

ecs_view_define(UpdateTrailView) {
  ecs_access_maybe_read(SceneLifetimeDurationComp);
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneSetMemberComp);
  ecs_access_maybe_read(SceneTagComp);
  ecs_access_maybe_read(SceneVisibilityComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_read(SceneVfxDecalComp);
  ecs_access_write(VfxDecalTrailComp);
}

typedef struct {
  GeoVector pos;
  f32       alpha;
} VfxTrailPoint;

static u32 vfx_decal_trail_history_index(const VfxDecalTrailComp* inst, const u32 age) {
  diag_assert(age < vfx_decal_trail_history_count);
  if (inst->historyNewest >= age) {
    return inst->historyNewest - age;
  }
  return vfx_decal_trail_history_count - (age - inst->historyNewest);
}

static u32 vfx_decal_trail_history_oldest(const VfxDecalTrailComp* inst) {
  return (inst->historyNewest + 1) % vfx_decal_trail_history_count;
}

static VfxTrailPoint vfx_decal_trail_history_get(VfxDecalTrailComp* inst, const u32 index) {
  VfxTrailPoint result;
  result.pos   = inst->history[index];
  result.alpha = inst->historyAlpha[index];
  return result;
}

static void vfx_decal_trail_history_reset(VfxDecalTrailComp* inst, const VfxTrailPoint point) {
  inst->historyNewest     = 0;
  inst->historyCountTotal = 0;
  for (u32 i = 0; i != vfx_decal_trail_history_count; ++i) {
    inst->history[i]      = point.pos;
    inst->historyAlpha[i] = point.alpha;
  }
}

static void vfx_decal_trail_history_add(VfxDecalTrailComp* inst, const VfxTrailPoint point) {
  const u32 indexOldest           = vfx_decal_trail_history_oldest(inst);
  inst->history[indexOldest]      = point.pos;
  inst->historyAlpha[indexOldest] = point.alpha;
  inst->historyNewest             = indexOldest;
  ++inst->historyCountTotal;
}

static VfxTrailPoint vfx_trail_point_extrapolate(const VfxTrailPoint a, const VfxTrailPoint b) {
  VfxTrailPoint result;
  result.pos   = geo_vector_add(b.pos, geo_vector_sub(b.pos, a.pos));
  result.alpha = b.alpha;
  return result;
}

/**
 * The trail spline consists out of the current head point followed by all the history points.
 * Additionally there's an extra control point at the beginning and end to control the curvature of
 * the first and last segments.
 */
static void vfx_decal_trail_spline_init(
    VfxDecalTrailComp*  inst,
    const VfxTrailPoint headPoint,
    VfxTrailPoint       out[PARAM_ARRAY_SIZE(vfx_decal_trail_spline_points)]) {
  const VfxTrailPoint newestPoint = vfx_decal_trail_history_get(inst, inst->historyNewest);

  u32 i    = 0;
  out[i++] = vfx_trail_point_extrapolate(newestPoint, headPoint);
  out[i++] = headPoint;
  for (u32 age = 0; age != vfx_decal_trail_history_count; ++age) {
    out[i++] = vfx_decal_trail_history_get(inst, vfx_decal_trail_history_index(inst, age));
  }
  out[i] = vfx_trail_point_extrapolate(out[i - 2], out[i - 1]);
  ++i;

  diag_assert(i == vfx_decal_trail_spline_points);
}

/**
 * Catmull-rom spline (cubic hermite) with uniform parametrization.
 * Ref: https://andrewhungblog.wordpress.com/2017/03/03/catmull-rom-splines-in-plain-english/
 * NOTE: Tension hardcoded to 0.
 */
static GeoVector vfx_catmullrom(
    const GeoVector a, const GeoVector b, const GeoVector c, const GeoVector d, const f32 t) {
  const f32 tSqr  = t * t;
  const f32 tCube = tSqr * t;

  GeoVector res;
  res = geo_vector_mul(a, -0.5f * tCube + 1.0f * tSqr - 0.5f * t);
  res = geo_vector_add(res, geo_vector_mul(b, 1.0f + 0.5f * tSqr * -5.0f + 0.5f * tCube * 3.0f));
  res = geo_vector_add(res, geo_vector_mul(c, 0.5f * tCube * -3.0f + 0.5f * t - -2.0f * tSqr));
  res = geo_vector_add(res, geo_vector_mul(d, -0.5f * tSqr + 0.5f * tCube));
  return res;
}

/**
 * Sample a position on the spline formed by the given points.
 * NOTE: The first and last are only control points, the spline will not pass through them.
 * NOTE: t = 0.0f results in points[1] and t = (count - 2) results in points[count - 2].
 */
static VfxTrailPoint vfx_spline_sample(const VfxTrailPoint* points, const u32 count, const f32 t) {
  static const f32 g_splineEpsilon = 1e-5f;
  const f32        tMin = 1.0f, tMax = (f32)count - 2.0f - g_splineEpsilon;
  const f32        tAbs  = math_min(t + tMin, tMax);
  const u32        index = (u32)math_round_down_f32(tAbs);
  const f32        frac  = tAbs - (f32)index;

  diag_assert(index > 0 && index < count - 2);

  const VfxTrailPoint a = points[index - 1];
  const VfxTrailPoint b = points[index];
  const VfxTrailPoint c = points[index + 1];
  const VfxTrailPoint d = points[index + 2];

  VfxTrailPoint sample;
  sample.pos   = vfx_catmullrom(a.pos, b.pos, c.pos, d.pos, frac);
  sample.alpha = math_lerp(b.alpha, c.alpha, frac);
  return sample;
}

typedef struct {
  GeoVector position;
  GeoVector normal, tangent;
  f32       length;
  f32       alphaBegin, alphaEnd;
  f32       splineBegin, splineEnd;
} VfxTrailSegment;

static GeoVector vfx_trail_segment_tangent_avg(const VfxTrailSegment* a, const VfxTrailSegment* b) {
  const GeoVector tanAvg = geo_vector_mul(geo_vector_add(a->tangent, b->tangent), 0.5f);
  return geo_vector_norm_or(tanAvg, a->tangent);
}

static void vfx_decal_trail_update(
    const SceneTimeComp*          timeComp,
    const SceneTerrainComp*       terrainComp,
    const SceneVisibilityEnvComp* visEnv,
    RendDrawComp*                 drawNormal,
    RendDrawComp*                 drawDebug,
    EcsIterator*                  itr) {
  VfxDecalTrailComp*               inst      = ecs_view_write_t(itr, VfxDecalTrailComp);
  const SceneTransformComp*        trans     = ecs_view_read_t(itr, SceneTransformComp);
  const SceneScaleComp*            scaleComp = ecs_view_read_t(itr, SceneScaleComp);
  const SceneSetMemberComp*        setMember = ecs_view_read_t(itr, SceneSetMemberComp);
  const SceneVfxDecalComp*         decal     = ecs_view_read_t(itr, SceneVfxDecalComp);
  const SceneTagComp*              tagComp   = ecs_view_read_t(itr, SceneTagComp);
  const SceneLifetimeDurationComp* lifetime  = ecs_view_read_t(itr, SceneLifetimeDurationComp);
  const SceneVisibilityComp*       visComp   = ecs_view_read_t(itr, SceneVisibilityComp);

  bool shouldEmit = true;
  if (tagComp && !(tagComp->tags & SceneTags_Emit)) {
    shouldEmit = false;
  }
  if (visComp && !scene_visible_for_render(visEnv, visComp)) {
    shouldEmit = false;
  }

  VfxTrailPoint headPoint = {.pos = trans->position, .alpha = shouldEmit ? 1.0f : 0.0f};
  if (inst->snapToTerrain) {
    scene_terrain_snap(terrainComp, &headPoint.pos);
  }

  const bool      debug   = setMember && scene_set_member_contains(setMember, g_sceneSetSelected);
  const f32       fadeIn  = vfx_decal_fade_in(timeComp, inst->creationTime, inst->fadeInSec);
  const f32       fadeOut = vfx_decal_fade_out(lifetime, inst->fadeOutSec);
  const GeoVector projAxisRef    = geo_up; // TODO: Make the projection axis configurable.
  const f32       trailAlpha     = decal->alpha * inst->alpha * fadeIn * fadeOut;
  const f32       trailScale     = scaleComp ? scaleComp->scale : 1.0f;
  const f32       trailSpacing   = inst->pointSpacing * trailScale;
  const f32       trailWidth     = inst->width * trailScale;
  const f32       trailHeight    = inst->height * trailScale;
  const f32       trailWidthInv  = 1.0f / trailWidth;
  const f32       trailTexYScale = trailSpacing / trailHeight;

  if (inst->trailFlags & VfxTrailFlags_HistoryReset) {
    vfx_decal_trail_history_reset(inst, headPoint);
    inst->trailFlags &= ~VfxTrailFlags_HistoryReset;
  }

  // Append to the history if we've moved enough.
  const GeoVector newestPos  = inst->history[inst->historyNewest];
  const GeoVector toHead     = geo_vector_sub(headPoint.pos, newestPos);
  const f32       toHeadFrac = geo_vector_mag(toHead) / trailSpacing;
  if (toHeadFrac >= 1.0f) {
    vfx_decal_trail_history_add(inst, headPoint);
    inst->nextPointFrac = 0.0f;
  } else {
    inst->nextPointFrac = math_max(inst->nextPointFrac, toHeadFrac);
  }

  // Construct the spline control points.
  VfxTrailPoint spline[vfx_decal_trail_spline_points];
  vfx_decal_trail_spline_init(inst, headPoint, spline);

  // Compute trail segments by sampling the spline.
  VfxTrailSegment segs[vfx_decal_trail_seg_count_max];
  u32             segCount = 0;
  const f32       tMax     = (f32)(vfx_decal_trail_history_count + 1);
  const f32       tStep    = vfx_decal_trail_step;
  VfxTrailPoint   segBegin = headPoint;
  for (f32 t = tStep, tLast = 0; t < tMax && segCount != array_elems(segs); t += tStep) {
    const VfxTrailPoint segEnd       = vfx_spline_sample(spline, array_elems(spline), t);
    const GeoVector     segDelta     = geo_vector_sub(segEnd.pos, segBegin.pos);
    const f32           segLengthSqr = geo_vector_mag_sqr(segDelta);
    if (segLengthSqr < (vfx_decal_trail_seg_min_length * vfx_decal_trail_seg_min_length)) {
      continue;
    }
    const f32       segLength     = math_sqrt_f32(segLengthSqr);
    const GeoVector segCenter     = geo_vector_mul(geo_vector_add(segBegin.pos, segEnd.pos), 0.5f);
    const GeoVector segNormal     = geo_vector_div(segDelta, segLength);
    const GeoVector segTangentRaw = geo_vector_cross3(segNormal, projAxisRef);
    const f32       segTangentLen = geo_vector_mag(segTangentRaw);
    if (segTangentLen < f32_epsilon) {
      continue;
    }
    const GeoVector segTangent = geo_vector_div(segTangentRaw, segTangentLen);

    segs[segCount++] = (VfxTrailSegment){
        .position    = segCenter,
        .normal      = segNormal,
        .tangent     = segTangent,
        .length      = segLength,
        .alphaBegin  = segBegin.alpha,
        .alphaEnd    = segEnd.alpha,
        .splineBegin = tLast,
        .splineEnd   = t,
    };
    segBegin = segEnd;
    tLast    = t;
  }

  // Emit decals for the segments.
  // NOTE: '1.0 -' because we are modelling the texture space growing backwards not forwards.
  f32 texOffset = 1.0f - math_mod_f32((f32)inst->historyCountTotal * trailTexYScale, 1.0f);
  for (u32 i = 0; i != segCount; ++i) {
    const VfxTrailSegment* seg     = &segs[i];
    const VfxTrailSegment* segPrev = i ? &segs[i - 1] : seg;
    const VfxTrailSegment* segNext = (i != (segCount - 1)) ? &segs[i + 1] : seg;

    const GeoVector projAxis     = geo_vector_cross3(seg->tangent, seg->normal);
    const GeoMatrix segRot       = geo_matrix_rotate(seg->tangent, seg->normal, projAxis);
    const GeoQuat   rot          = geo_matrix_to_quat(&segRot);
    const GeoQuat   rotInv       = geo_quat_inverse(rot);
    const f32       segAspect    = seg->length * trailWidthInv;
    const f32       segAspectInv = 1.0f / segAspect;
    const f32       segTexScale  = (seg->splineEnd - seg->splineBegin) * trailTexYScale;

    const GeoVector tangentBegin = vfx_trail_segment_tangent_avg(seg, segPrev);
    const GeoVector tangentEnd   = vfx_trail_segment_tangent_avg(seg, segNext);

    /**
     * Compute a warp (3x3 transformation matrix) to deform our rectangle decals so that they will
     * seamlessly connect.
     * NOTE: Only works when resulting quad is convex, if not then there will be visible gaps or
     * overlaps.
     */

    const GeoVector localTangentBegin = geo_quat_rotate(rotInv, tangentBegin);
    const GeoVector localTangentEnd   = geo_quat_rotate(rotInv, tangentEnd);

    const VfxWarpVec warpTangentBegin = {localTangentBegin.x, localTangentBegin.y * segAspectInv};
    const VfxWarpVec warpTangentEnd   = {localTangentEnd.x, localTangentEnd.y * segAspectInv};

    const VfxWarpVec corners[4] = {
        vfx_warp_vec_add((VfxWarpVec){0.5f, 0.0f}, vfx_warp_vec_mul(warpTangentBegin, 0.5f)),
        vfx_warp_vec_sub((VfxWarpVec){0.5f, 0.0f}, vfx_warp_vec_mul(warpTangentBegin, 0.5f)),
        vfx_warp_vec_sub((VfxWarpVec){0.5f, 1.0f}, vfx_warp_vec_mul(warpTangentEnd, 0.5f)),
        vfx_warp_vec_add((VfxWarpVec){0.5f, 1.0f}, vfx_warp_vec_mul(warpTangentEnd, 0.5f)),
    };

    const f32 splineBegin         = seg->splineBegin + inst->nextPointFrac;
    const f32 splineEnd           = seg->splineEnd + inst->nextPointFrac;
    const f32 splineFadeThreshold = (f32)vfx_decal_trail_history_count - 1.0f;
    const f32 alphaBegin = (1.0f - math_max(0.0f, splineBegin - splineFadeThreshold)) * trailAlpha;
    const f32 alphaEnd   = (1.0f - math_max(0.0f, splineEnd - splineFadeThreshold)) * trailAlpha;

    const VfxDecalParams params = {
        .pos              = seg->position,
        .rot              = rot,
        .width            = inst->width,
        .height           = seg->length,
        .thickness        = inst->thickness,
        .flags            = inst->decalFlags,
        .excludeTags      = inst->excludeTags,
        .atlasColorIndex  = inst->atlasColorIndex,
        .atlasNormalIndex = inst->atlasNormalIndex,
        .alphaBegin       = i ? alphaBegin * seg->alphaBegin : 0.0f,
        .alphaEnd         = alphaEnd * seg->alphaEnd,
        .roughness        = inst->roughness,
        .texOffsetY       = texOffset,
        .texScaleY        = segTexScale,
        .warpScale  = vfx_warp_bounds(corners, array_elems(corners), (VfxWarpVec){0.5f, 0.5f}),
        .warpPoints = {corners[0], corners[1], corners[2], corners[3]},
    };

    vfx_decal_draw_output(drawNormal, &params);
    if (UNLIKELY(debug)) {
      vfx_decal_draw_output(drawDebug, &params);
    }
    texOffset += segTexScale;
  }
}

ecs_system_define(VfxDecalTrailUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp*       timeComp     = ecs_view_read_t(globalItr, SceneTimeComp);
  const SceneTerrainComp*    terrainComp  = ecs_view_read_t(globalItr, SceneTerrainComp);
  const VfxAtlasManagerComp* atlasManager = ecs_view_read_t(globalItr, VfxAtlasManagerComp);
  const AssetAtlasComp*      atlasColor   = vfx_atlas(world, atlasManager, VfxAtlasType_DecalColor);
  const AssetAtlasComp*      atlasNormal = vfx_atlas(world, atlasManager, VfxAtlasType_DecalNormal);
  if (!atlasColor || !atlasNormal) {
    return; // Atlas hasn't loaded yet.
  }

  const SceneVisibilityEnvComp* visEnv      = ecs_view_read_t(globalItr, SceneVisibilityEnvComp);
  const VfxDrawManagerComp*     drawManager = ecs_view_read_t(globalItr, VfxDrawManagerComp);

  RendDrawComp* drawNormal = vfx_draw_get(world, drawManager, VfxDrawType_DecalTrail);
  RendDrawComp* drawDebug  = vfx_draw_get(world, drawManager, VfxDrawType_DecalTrailDebug);

  vfx_decal_draw_init(drawNormal, atlasColor, atlasNormal);
  vfx_decal_draw_init(drawDebug, atlasColor, atlasNormal);

  EcsView* trailView = ecs_world_view_t(world, UpdateTrailView);
  for (EcsIterator* itr = ecs_view_itr(trailView); ecs_view_walk(itr);) {
    vfx_decal_trail_update(timeComp, terrainComp, visEnv, drawNormal, drawDebug, itr);
  }
}

ecs_module_init(vfx_decal_module) {
  ecs_register_comp_empty(VfxDecalAnyComp);
  ecs_register_comp(VfxDecalSingleComp);
  ecs_register_comp(VfxDecalTrailComp);
  ecs_register_comp(VfxDecalAssetComp, .combinator = ecs_combine_decal_asset);

  ecs_register_view(GlobalView);
  ecs_register_view(AtlasView);
  ecs_register_view(DecalDrawView);
  ecs_register_view(DecalAnyView);

  ecs_register_system(VfxDecalLoadSys, ecs_register_view(LoadView), ecs_view_id(DecalAnyView));

  ecs_register_system(
      VfxDecalInitSys,
      ecs_register_view(InitView),
      ecs_register_view(InitAssetView),
      ecs_view_id(AtlasView),
      ecs_view_id(GlobalView));

  ecs_register_system(VfxDecalDeinitSys, ecs_register_view(DeinitView));

  ecs_register_system(
      VfxDecalSingleUpdateSys,
      ecs_register_view(UpdateSingleView),
      ecs_view_id(DecalDrawView),
      ecs_view_id(AtlasView),
      ecs_view_id(GlobalView));

  ecs_register_system(
      VfxDecalTrailUpdateSys,
      ecs_register_view(UpdateTrailView),
      ecs_view_id(DecalDrawView),
      ecs_view_id(AtlasView),
      ecs_view_id(GlobalView));

  ecs_order(VfxDecalSingleUpdateSys, VfxOrder_Update);
  ecs_order(VfxDecalTrailUpdateSys, VfxOrder_Update);
}
