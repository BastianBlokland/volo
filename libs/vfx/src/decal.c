#include "asset_atlas.h"
#include "asset_decal.h"
#include "asset_manager.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "core_rng.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "rend_draw.h"
#include "rend_fog.h"
#include "rend_instance.h"
#include "scene_lifetime.h"
#include "scene_set.h"
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
#define vfx_decal_trail_history_spacing 1.0f
#define vfx_decal_trail_spline_points (vfx_decal_trail_history_count + 3)
#define vfx_decal_trail_seg_min_length 0.2f
#define vfx_decal_trail_seg_count_max 64
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
  f32     data1[4]; // xyz: position, w: flags.
  f16     data2[4]; // xyzw: rotation quaternion.
  f16     data3[4]; // xyz: scale, w: excludeTags.
  f16     data4[4]; // x: atlasColorIndex, y: atlasNormalIndex, z: roughness, w: alpha.
  f16     data5[4]; // xy: warpScale, z: texOffsetY, w: texScaleY.
  VfxWarp warp;     // 3x3 warp matrix.
} VfxDecalData;

ASSERT(sizeof(VfxDecalData) == 96, "Size needs to match the size defined in glsl");

typedef enum {
  VfxLoad_Acquired  = 1 << 0,
  VfxLoad_Unloading = 1 << 1,
} VfxLoadFlags;

ecs_comp_define(VfxDecalAnyComp);

ecs_comp_define(VfxDecalSingleComp) {
  u16            atlasColorIndex, atlasNormalIndex;
  VfxDecalFlags  flags : 8;
  AssetDecalAxis axis : 8;
  u8             excludeTags; // First 8 entries of SceneTags are supported.
  f32            angle;
  f32            roughness, alpha;
  f32            fadeInSec, fadeOutSec;
  f32            width, height, thickness;
  TimeDuration   creationTime;
};

typedef struct {
  GeoVector pos;
  GeoVector dir; // Projection axis.
} VfxTrailPoint;

ecs_comp_define(VfxDecalTrailComp) {
  u16            atlasColorIndex, atlasNormalIndex;
  VfxDecalFlags  flags : 8;
  AssetDecalAxis axis : 8;
  u8             excludeTags; // First 8 entries of SceneTags are supported.
  bool           historyReset;
  f32            roughness, alpha;
  f32            width, thickness;
  u32            historyNewest;
  VfxTrailPoint  history[vfx_decal_trail_history_count];
};

ecs_comp_define(VfxDecalAssetComp) { VfxLoadFlags loadFlags; };

static void ecs_combine_decal_asset(void* dataA, void* dataB) {
  VfxDecalAssetComp* compA = dataA;
  VfxDecalAssetComp* compB = dataB;
  compA->loadFlags |= compB->loadFlags;
}

ecs_view_define(GlobalView) {
  ecs_access_read(SceneTimeComp);
  ecs_access_read(SceneVisibilityEnvComp);
  ecs_access_read(VfxAtlasManagerComp);
  ecs_access_read(VfxDrawManagerComp);
}

ecs_view_define(AtlasView) { ecs_access_read(AssetAtlasComp); }

ecs_view_define(DecalDrawView) {
  ecs_access_with(VfxDrawDecalComp);
  ecs_access_write(RendDrawComp);

  /**
   * Mark the draws as explicitly exclusive with other types of draws.
   * This allows the scheduler to run the draw filling in parallel with other draw filling.
   */
  ecs_access_without(VfxDrawParticleComp);
  ecs_access_without(RendInstanceDrawComp);
  ecs_access_without(RendFogDrawComp);
}

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
      .flags            = vfx_decal_flags(asset),
      .axis             = asset->projectionAxis,
      .excludeTags      = vfx_decal_mask_to_tags(asset->excludeMask),
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
    const AssetDecalComp* asset) {

  const f32 alpha = rng_sample_range(g_rng, asset->alphaMin, asset->alphaMax);
  const f32 scale = rng_sample_range(g_rng, asset->scaleMin, asset->scaleMax);
  ecs_world_add_empty_t(world, entity, VfxDecalAnyComp);
  ecs_world_add_t(
      world,
      entity,
      VfxDecalTrailComp,
      .historyReset     = true,
      .atlasColorIndex  = atlasColorIndex,
      .atlasNormalIndex = atlasNormalIndex,
      .flags            = vfx_decal_flags(asset),
      .axis             = asset->projectionAxis,
      .excludeTags      = vfx_decal_mask_to_tags(asset->excludeMask),
      .roughness        = asset->roughness,
      .alpha            = alpha,
      .width            = asset->width * scale,
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
      vfx_decal_create_trail(world, e, atlasColorIndex, atlasNormalIndex, asset);
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
  f32           alpha, roughness;
  f32           width, height, thickness;
  f32           texOffsetY, texScaleY;
  VfxWarpVec    warpScale;
  VfxWarp       warp;
} VfxDecalParams;

static void vfx_decal_draw_output(RendDrawComp* draw, const VfxDecalParams* params) {
  const GeoVector decalSize = geo_vector(params->width, params->height, params->thickness);
  const GeoVector warpScale = geo_vector(params->warpScale.x, params->warpScale.y, 1);

  const GeoBox box = geo_box_from_center(params->pos, geo_vector_mul_comps(decalSize, warpScale));
  const GeoBox bounds = geo_box_from_rotated(&box, params->rot);

  VfxDecalData* out = rend_draw_add_instance_t(draw, VfxDecalData, SceneTags_Vfx, bounds);
  mem_cpy(array_mem(out->data1), mem_create(params->pos.comps, sizeof(f32) * 3));
  out->data1[3] = (f32)params->flags;

  geo_quat_pack_f16(params->rot, out->data2);

  out->data3[0] = float_f32_to_f16(decalSize.x);
  out->data3[1] = float_f32_to_f16(decalSize.y);
  out->data3[2] = float_f32_to_f16(decalSize.z);
  out->data3[3] = float_f32_to_f16((u32)params->excludeTags);

  diag_assert_msg(params->atlasColorIndex <= 1024, "Index not representable by 16 bit float");
  diag_assert_msg(params->atlasNormalIndex <= 1024, "Index not representable by 16 bit float");

  out->data4[0] = float_f32_to_f16((f32)params->atlasColorIndex);
  out->data4[1] = float_f32_to_f16((f32)params->atlasNormalIndex);
  out->data4[2] = float_f32_to_f16(params->roughness);
  out->data4[3] = float_f32_to_f16(params->alpha);

  out->data5[0] = float_f32_to_f16(warpScale.x);
  out->data5[1] = float_f32_to_f16(warpScale.y);
  out->data5[2] = float_f32_to_f16(params->texOffsetY);
  out->data5[3] = float_f32_to_f16(params->texScaleY);

  out->warp = params->warp;
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
    const SceneTimeComp*           timeComp,
    const SceneVisibilitySettings* visibilitySettings,
    RendDrawComp*                  drawNormal,
    RendDrawComp*                  drawDebug,
    EcsIterator*                   itr) {
  const VfxDecalSingleComp*        inst      = ecs_view_read_t(itr, VfxDecalSingleComp);
  const SceneTransformComp*        trans     = ecs_view_read_t(itr, SceneTransformComp);
  const SceneScaleComp*            scaleComp = ecs_view_read_t(itr, SceneScaleComp);
  const SceneSetMemberComp*        setMember = ecs_view_read_t(itr, SceneSetMemberComp);
  const SceneVfxDecalComp*         decal     = ecs_view_read_t(itr, SceneVfxDecalComp);
  const SceneLifetimeDurationComp* lifetime  = ecs_view_read_t(itr, SceneLifetimeDurationComp);

  const SceneVisibilityComp* visComp = ecs_view_read_t(itr, SceneVisibilityComp);
  if (visComp && !scene_visible(visComp, SceneFaction_A) && !visibilitySettings->renderAll) {
    return; // TODO: Make the local faction configurable instead of hardcoding 'A'.
  }

  const bool debug = setMember && scene_set_member_contains(setMember, g_sceneSetSelected);

  const GeoQuat        rotRaw = vfx_decal_rotation(trans->rotation, inst->axis);
  const GeoQuat        rot    = geo_quat_mul(rotRaw, geo_quat_angle_axis(inst->angle, geo_forward));
  const f32            scale  = scaleComp ? scaleComp->scale : 1.0f;
  const f32            fadeIn = vfx_decal_fade_in(timeComp, inst->creationTime, inst->fadeInSec);
  const f32            fadeOut = vfx_decal_fade_out(lifetime, inst->fadeOutSec);
  const VfxDecalParams params  = {
       .pos              = trans->position,
       .rot              = rot,
       .width            = inst->width * scale,
       .height           = inst->height * scale,
       .thickness        = inst->thickness,
       .flags            = inst->flags,
       .excludeTags      = inst->excludeTags,
       .atlasColorIndex  = inst->atlasColorIndex,
       .atlasNormalIndex = inst->atlasNormalIndex,
       .alpha            = decal->alpha * inst->alpha * fadeIn * fadeOut,
       .roughness        = inst->roughness,
       .texOffsetY       = 0.0f,
       .texScaleY        = 1.0f,
       .warpScale        = {1.0f, 1.0f},
       .warp             = vfx_warp_ident(),
  };

  vfx_decal_draw_output(drawNormal, &params);
  if (UNLIKELY(debug)) {
    vfx_decal_draw_output(drawDebug, &params);
  }
}

ecs_view_define(UpdateTrailView) {
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneSetMemberComp);
  ecs_access_maybe_read(SceneVisibilityComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_read(SceneVfxDecalComp);
  ecs_access_read(SceneVfxDecalComp);
  ecs_access_write(VfxDecalTrailComp);
}

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

static void vfx_decal_trail_history_reset(VfxDecalTrailComp* inst, const VfxTrailPoint point) {
  inst->historyNewest = 0;
  for (u32 i = 0; i != vfx_decal_trail_history_count; ++i) {
    inst->history[i] = point;
  }
}

static void vfx_decal_trail_history_add(VfxDecalTrailComp* inst, const VfxTrailPoint point) {
  const u32 indexOldest      = vfx_decal_trail_history_oldest(inst);
  inst->history[indexOldest] = point;
  inst->historyNewest        = indexOldest;
}

static VfxTrailPoint vfx_trail_point_center(const VfxTrailPoint from, const VfxTrailPoint to) {
  VfxTrailPoint res;
  res.pos = geo_vector_mul(geo_vector_add(from.pos, to.pos), 0.5f);
  res.dir = geo_vector_norm_or(geo_vector_mul(geo_vector_add(from.dir, to.dir), 0.5f), geo_up);
  return res;
}

static VfxTrailPoint vfx_trail_point_extrapolate(const VfxTrailPoint from, const VfxTrailPoint to) {
  VfxTrailPoint res;
  res.pos = geo_vector_add(to.pos, geo_vector_sub(to.pos, from.pos));
  res.dir = to.dir; // TODO: Extrapolate projection direction.
  return res;
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
  u32 i    = 0;
  out[i++] = vfx_trail_point_extrapolate(inst->history[inst->historyNewest], headPoint);
  out[i++] = headPoint;
  for (u32 age = 0; age != vfx_decal_trail_history_count; ++age) {
    out[i++] = inst->history[vfx_decal_trail_history_index(inst, age)];
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

  VfxTrailPoint res;
  res.pos = vfx_catmullrom(a.pos, b.pos, c.pos, d.pos, frac);
  res.dir = geo_vector_norm_or(geo_vector_lerp(b.dir, c.dir, frac), geo_up);
  return res;
}

typedef struct {
  GeoVector position;
  GeoVector normal, tangent;
  f32       length;
} VfxTrailSegment;

static GeoVector vfx_trail_segment_tangent_avg(const VfxTrailSegment* a, const VfxTrailSegment* b) {
  const GeoVector tanAvg = geo_vector_mul(geo_vector_add(a->tangent, b->tangent), 0.5f);
  return geo_vector_norm_or(tanAvg, a->tangent);
}

static void vfx_decal_trail_update(
    const SceneVisibilitySettings* visibilitySettings,
    RendDrawComp*                  drawNormal,
    RendDrawComp*                  drawDebug,
    EcsIterator*                   itr) {
  VfxDecalTrailComp*        inst      = ecs_view_write_t(itr, VfxDecalTrailComp);
  const SceneTransformComp* trans     = ecs_view_read_t(itr, SceneTransformComp);
  const SceneScaleComp*     scaleComp = ecs_view_read_t(itr, SceneScaleComp);
  const SceneSetMemberComp* setMember = ecs_view_read_t(itr, SceneSetMemberComp);
  const SceneVfxDecalComp*  decal     = ecs_view_read_t(itr, SceneVfxDecalComp);

  const SceneVisibilityComp* visComp = ecs_view_read_t(itr, SceneVisibilityComp);
  if (visComp && !scene_visible(visComp, SceneFaction_A) && !visibilitySettings->renderAll) {
    // TODO: Make the local faction configurable instead of hardcoding 'A'.
    // TODO: This should probably be per segment instead of the whole trail.
    return;
  }

  const VfxTrailPoint headPoint = {
      .pos = trans->position,
      .dir = geo_quat_rotate(vfx_decal_rotation(trans->rotation, inst->axis), geo_forward),
  };
  const bool debug         = setMember && scene_set_member_contains(setMember, g_sceneSetSelected);
  const f32  trailAlpha    = decal->alpha * inst->alpha;
  const f32  trailScale    = scaleComp ? scaleComp->scale : 1.0f;
  const f32  trailWidth    = inst->width * trailScale;
  const f32  trailWidthInv = 1.0f / trailWidth;

  if (inst->historyReset) {
    vfx_decal_trail_history_reset(inst, headPoint);
    inst->historyReset = false;
  }

  // Append to the history if we've moved enough.
  const GeoVector newestPos = inst->history[inst->historyNewest].pos;
  const GeoVector delta     = geo_vector_sub(trans->position, newestPos);
  const f32       distSqr   = geo_vector_mag_sqr(delta);
  if (distSqr > (vfx_decal_trail_history_spacing * vfx_decal_trail_history_spacing)) {
    vfx_decal_trail_history_add(inst, headPoint);
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
  for (f32 t = tStep; t < tMax && segCount != array_elems(segs); t += tStep) {
    const VfxTrailPoint segEnd       = vfx_spline_sample(spline, array_elems(spline), t);
    const GeoVector     segDelta     = geo_vector_sub(segEnd.pos, segBegin.pos);
    const f32           segLengthSqr = geo_vector_mag_sqr(segDelta);
    if (segLengthSqr < (vfx_decal_trail_seg_min_length * vfx_decal_trail_seg_min_length)) {
      continue;
    }
    const f32           segLength  = math_sqrt_f32(segLengthSqr);
    const VfxTrailPoint segCenter  = vfx_trail_point_center(segBegin, segEnd);
    const GeoVector     segNormal  = geo_vector_div(segDelta, segLength);
    const GeoVector     segTangent = geo_vector_norm(geo_vector_cross3(segNormal, segCenter.dir));

    segs[segCount++] = (VfxTrailSegment){
        .position = segCenter.pos,
        .normal   = segNormal,
        .tangent  = segTangent,
        .length   = segLength,
    };
    segBegin = segEnd;
  }

  // Emit decals for the segments.
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

    VfxWarpVec corners[4] = {
        vfx_warp_vec_sub((VfxWarpVec){0.5f, 0.0f}, vfx_warp_vec_mul(warpTangentBegin, 0.5f)),
        vfx_warp_vec_add((VfxWarpVec){0.5f, 0.0f}, vfx_warp_vec_mul(warpTangentBegin, 0.5f)),
        vfx_warp_vec_add((VfxWarpVec){0.5f, 1.0f}, vfx_warp_vec_mul(warpTangentEnd, 0.5f)),
        vfx_warp_vec_sub((VfxWarpVec){0.5f, 1.0f}, vfx_warp_vec_mul(warpTangentEnd, 0.5f)),
    };

    if (!vfx_warp_is_convex(corners, array_elems(corners))) {
      /**
       * The quad is concave (which we cannot represent with a warp), make it convex by making the
       * left and the right edges parallel to each other. This still results in visible gaps and/or
       * overlap but its allot better then skipping the segment altogether.
       */
      const VfxWarpVec edgeA = vfx_warp_vec_sub(corners[0], corners[3]);
      const VfxWarpVec edgeB = vfx_warp_vec_sub(corners[1], corners[2]);
      if (math_abs(edgeA.y) < math_abs(edgeB.y)) {
        corners[0] = vfx_warp_vec_add(corners[3], vfx_warp_vec_project_forward(edgeA, edgeB));
      } else {
        corners[1] = vfx_warp_vec_add(corners[2], vfx_warp_vec_project_forward(edgeB, edgeA));
      }
    }

    if (!vfx_warp_is_convex(corners, array_elems(corners))) {
      continue; // Quad was still concave after attempting to fix it; discard the segment.
    }

    const VfxDecalParams params = {
        .pos              = seg->position,
        .rot              = rot,
        .width            = inst->width,
        .height           = seg->length,
        .thickness        = inst->thickness,
        .flags            = inst->flags,
        .excludeTags      = inst->excludeTags,
        .atlasColorIndex  = inst->atlasColorIndex,
        .atlasNormalIndex = inst->atlasNormalIndex,
        .alpha            = trailAlpha,
        .roughness        = inst->roughness,
        .texOffsetY       = 0.0f,
        .texScaleY        = 1.0f,
        .warpScale = vfx_warp_bounds(corners, array_elems(corners), (VfxWarpVec){0.5f, 0.5f}),
        .warp      = vfx_warp_from_points(corners),
    };

    vfx_decal_draw_output(drawNormal, &params);
    if (UNLIKELY(debug)) {
      vfx_decal_draw_output(drawDebug, &params);
    }
  }
}

ecs_system_define(VfxDecalUpdateSys) {
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

  const SceneVisibilityEnvComp*  visibilityEnv = ecs_view_read_t(globalItr, SceneVisibilityEnvComp);
  const SceneVisibilitySettings* visibilitySettings = scene_visibility_settings(visibilityEnv);

  const VfxDrawManagerComp* drawManager = ecs_view_read_t(globalItr, VfxDrawManagerComp);

  RendDrawComp* drawNormal = vfx_draw_get(world, drawManager, VfxDrawType_Decal);
  RendDrawComp* drawDebug  = vfx_draw_get(world, drawManager, VfxDrawType_DecalDebug);

  vfx_decal_draw_init(drawNormal, atlasColor, atlasNormal);
  vfx_decal_draw_init(drawDebug, atlasColor, atlasNormal);

  // Update all single decals.
  EcsView* singleView = ecs_world_view_t(world, UpdateSingleView);
  for (EcsIterator* itr = ecs_view_itr(singleView); ecs_view_walk(itr);) {
    vfx_decal_single_update(timeComp, visibilitySettings, drawNormal, drawDebug, itr);
  }

  // Update all trail decals.
  EcsView* trailView = ecs_world_view_t(world, UpdateTrailView);
  for (EcsIterator* itr = ecs_view_itr(trailView); ecs_view_walk(itr);) {
    vfx_decal_trail_update(visibilitySettings, drawNormal, drawDebug, itr);
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
      VfxDecalUpdateSys,
      ecs_register_view(UpdateSingleView),
      ecs_register_view(UpdateTrailView),
      ecs_view_id(DecalDrawView),
      ecs_view_id(AtlasView),
      ecs_view_id(GlobalView));

  ecs_order(VfxDecalUpdateSys, VfxOrder_Update);
}
