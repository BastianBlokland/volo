#include "asset_atlas.h"
#include "asset_manager.h"
#include "asset_vfx.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_rng.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_lifetime.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "scene_vfx.h"
#include "vfx_register.h"

#include "particle_internal.h"

#define vfx_max_asset_requests 4

typedef struct {
  u8           emitter;
  u16          atlasBaseIndex;
  f32          speed;
  TimeDuration lifetime;
  TimeDuration age;
  GeoVector    pos;
  GeoVector    dir;
} VfxInstance;

typedef struct {
  u32 spawnCount;
} VfxEmitterState;

ecs_comp_define(VfxStateComp) {
  TimeDuration    age;
  GeoVector       prevPos;
  VfxEmitterState emitters[asset_vfx_max_emitters];
  DynArray        instances; // VfxInstance[].
};

typedef enum {
  VfxAsset_Acquired  = 1 << 0,
  VfxAsset_Unloading = 1 << 1,
} VfxAssetRequestFlags;

ecs_comp_define(VfxAssetRequestComp) { VfxAssetRequestFlags flags; };

static void ecs_destruct_vfx_state_comp(void* data) {
  VfxStateComp* comp = data;
  dynarray_destroy(&comp->instances);
}

static void ecs_combine_asset_request(void* dataA, void* dataB) {
  VfxAssetRequestComp* compA = dataA;
  VfxAssetRequestComp* compB = dataB;
  compA->flags |= compB->flags;
}

ecs_view_define(DrawView) { ecs_access_write(RendDrawComp); }
ecs_view_define(AtlasView) { ecs_access_read(AssetAtlasComp); }
ecs_view_define(AssetView) { ecs_access_read(AssetVfxComp); }

static const AssetAtlasComp* vfx_atlas(EcsWorld* world, const EcsEntityId atlasEntity) {
  EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(world, AtlasView), atlasEntity);
  return LIKELY(itr) ? ecs_view_read_t(itr, AssetAtlasComp) : null;
}

static bool vfx_asset_request(EcsWorld* world, const EcsEntityId assetEntity) {
  if (!ecs_world_has_t(world, assetEntity, VfxAssetRequestComp)) {
    ecs_world_add_t(world, assetEntity, VfxAssetRequestComp);
    return true;
  }
  return false;
}

ecs_view_define(InitView) {
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_with(SceneVfxComp);
  ecs_access_without(VfxStateComp);
}

ecs_system_define(VfxStateInitSys) {
  EcsView* initView = ecs_world_view_t(world, InitView);
  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    const EcsEntityId         e     = ecs_view_entity(itr);
    const SceneTransformComp* trans = ecs_view_read_t(itr, SceneTransformComp);
    const GeoVector           pos   = LIKELY(trans) ? trans->position : geo_vector(0);

    ecs_world_add_t(
        world,
        e,
        VfxStateComp,
        .prevPos   = pos,
        .instances = dynarray_create_t(g_alloc_heap, VfxInstance, 4));
  }
}

ecs_view_define(DeinitView) {
  ecs_access_with(VfxStateComp);
  ecs_access_without(SceneVfxComp);
}

ecs_system_define(VfxStateDeinitSys) {
  EcsView* deinitView = ecs_world_view_t(world, DeinitView);
  for (EcsIterator* itr = ecs_view_itr(deinitView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, VfxStateComp);
  }
}

ecs_view_define(LoadView) { ecs_access_write(VfxAssetRequestComp); }

ecs_system_define(VfxAssetLoadSys) {
  EcsView* loadView = ecs_world_view_t(world, LoadView);
  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId    entity     = ecs_view_entity(itr);
    VfxAssetRequestComp* request    = ecs_view_write_t(itr, VfxAssetRequestComp);
    const bool           isLoaded   = ecs_world_has_t(world, entity, AssetLoadedComp);
    const bool           isFailed   = ecs_world_has_t(world, entity, AssetFailedComp);
    const bool           hasChanged = ecs_world_has_t(world, entity, AssetChangedComp);

    if (request->flags & VfxAsset_Acquired && (isLoaded || isFailed) && hasChanged) {
      asset_release(world, entity);
      request->flags &= ~VfxAsset_Acquired;
      request->flags |= VfxAsset_Unloading;
    }
    if (request->flags & VfxAsset_Unloading && !isLoaded) {
      request->flags &= ~VfxAsset_Unloading;
    }
    if (!(request->flags & (VfxAsset_Acquired | VfxAsset_Unloading))) {
      asset_acquire(world, entity);
      request->flags |= VfxAsset_Acquired;
    }
  }
}

ecs_view_define(UpdateGlobalView) {
  ecs_access_read(SceneTimeComp);
  ecs_access_read(VfxParticleRendererComp);
}

ecs_view_define(UpdateView) {
  ecs_access_maybe_read(SceneLifetimeDurationComp);
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_read(SceneVfxComp);
  ecs_access_write(VfxStateComp);
}

static GeoVector vfx_random_dir_in_cone(const AssetVfxCone* cone) {
  /**
   * Compute a uniformly distributed direction inside the given cone.
   * Reference: http://www.realtimerendering.com/resources/RTNews/html/rtnv20n1.html#art11
   */
  const f32 coneAngleCos = math_cos_f32(cone->angle);
  const f32 phi          = 2.0f * math_pi_f32 * rng_sample_f32(g_rng);
  const f32 z            = coneAngleCos + (1 - coneAngleCos) * rng_sample_f32(g_rng);
  const f32 sinT         = math_sqrt_f32(1 - z * z);
  const f32 x            = math_cos_f32(phi) * sinT;
  const f32 y            = math_sin_f32(phi) * sinT;
  return geo_quat_rotate(cone->rotation, geo_vector(x, y, z));
}

static f32 vfx_sample_range_scalar(const AssetVfxRangeScalar* scalar) {
  return rng_sample_range(g_rng, scalar->min, scalar->max);
}

static TimeDuration vfx_sample_range_duration(const AssetVfxRangeDuration* duration) {
  return (TimeDuration)rng_sample_range(g_rng, duration->min, duration->max);
}

static void vfx_blend_mode_apply(
    const GeoColor color, const AssetVfxBlend mode, GeoColor* outColor, f32* outOpacity) {
  switch (mode) {
  case AssetVfxBlend_None:
    *outColor   = geo_color(color.r, color.g, color.b, 1.0f);
    *outOpacity = 1.0f;
    return;
  case AssetVfxBlend_Alpha:
    *outColor   = color;
    *outOpacity = color.a;
    return;
  case AssetVfxBlend_Additive:
    *outColor   = color;
    *outOpacity = 0.0f;
    return;
  case AssetVfxBlend_AdditiveDouble:
    *outColor = geo_color(color.r * 2, color.g * 2, color.b * 2, color.a * 2), *outOpacity = 0.0f;
    return;
  case AssetVfxBlend_AdditiveQuad:
    *outColor = geo_color(color.r * 4, color.g * 4, color.b * 4, color.a * 4), *outOpacity = 0.0f;
    return;
  }
  UNREACHABLE
}

static VfxParticleFlags vfx_facing_particle_flags(const AssetVfxFacing facing) {
  switch (facing) {
  case AssetVfxFacing_World:
    return 0;
  case AssetVfxFacing_BillboardSphere:
    return VfxParticle_BillboardSphere;
  case AssetVfxFacing_BillboardCylinder:
    return VfxParticle_BillboardCylinder;
  }
  UNREACHABLE
}

static void vfx_system_spawn(
    VfxStateComp* state, const AssetVfxComp* asset, const AssetAtlasComp* atlas, const u8 emitter) {
  diag_assert(emitter < asset->emitterCount);
  const AssetVfxEmitter* emitterAsset = &asset->emitters[emitter];

  const AssetAtlasEntry* atlasEntry = asset_atlas_lookup(atlas, emitterAsset->atlasEntry);
  if (UNLIKELY(!atlasEntry)) {
    log_e("Vfx atlas entry missing", log_param("entry-hash", fmt_int(emitterAsset->atlasEntry)));
    return;
  }
  if (UNLIKELY(atlasEntry->atlasIndex + emitterAsset->flipbookCount > atlas->entryCount)) {
    log_e(
        "Vfx atlas has not enough entries for flipbook",
        log_param("atlas-entry-count", fmt_int(atlas->entryCount)),
        log_param("flipbook-count", fmt_int(emitterAsset->flipbookCount)));
    return;
  }

  diag_assert_msg(atlasEntry->atlasIndex <= u16_max, "Atlas index exceeds limit");

  *dynarray_push_t(&state->instances, VfxInstance) = (VfxInstance){
      .emitter        = emitter,
      .atlasBaseIndex = (u16)atlasEntry->atlasIndex,
      .speed          = vfx_sample_range_scalar(&emitterAsset->speed),
      .lifetime       = vfx_sample_range_duration(&emitterAsset->lifetime),
      .pos            = emitterAsset->cone.position,
      .dir            = vfx_random_dir_in_cone(&emitterAsset->cone),
  };
}

static u32 vfx_emitter_count(const AssetVfxEmitter* emitterAsset, const TimeDuration age) {
  if (emitterAsset->interval) {
    const u32 maxCount = emitterAsset->count ? emitterAsset->count : u32_max;
    return math_min(1 + (u32)(age / emitterAsset->interval), maxCount);
  }
  return math_max(1, emitterAsset->count);
}

static void vfx_system_simulate(
    VfxStateComp*         state,
    const AssetVfxComp*   asset,
    const AssetAtlasComp* atlas,
    const SceneTimeComp*  time,
    const f32             sysScale) {

  const f32 deltaSec = scene_delta_seconds(time);

  // Update shared state.
  state->age += time->delta;

  // Update emitters.
  for (u32 emitter = 0; emitter != asset->emitterCount; ++emitter) {
    VfxEmitterState*       emitterState = &state->emitters[emitter];
    const AssetVfxEmitter* emitterAsset = &asset->emitters[emitter];

    const u32 count = vfx_emitter_count(emitterAsset, state->age);
    for (; emitterState->spawnCount < count; ++emitterState->spawnCount) {
      vfx_system_spawn(state, asset, atlas, emitter);
    }
  }

  // Update instances.
  VfxInstance* instances = dynarray_begin_t(&state->instances, VfxInstance);
  for (u32 i = (u32)state->instances.size; i-- != 0;) {
    VfxInstance* instance = instances + i;

    // Apply movement.
    const GeoVector posDelta = geo_vector_mul(instance->dir, instance->speed * sysScale * deltaSec);
    instance->pos            = geo_vector_add(instance->pos, posDelta);

    // Update age and destruct if too old.
    if ((instance->age += time->delta) > instance->lifetime) {
      goto Destruct;
    }
    continue;

  Destruct:
    dynarray_remove_unordered(&state->instances, i, 1);
  }
}

static void vfx_instance_output(
    const VfxInstance*  instance,
    RendDrawComp*       draw,
    const AssetVfxComp* asset,
    const GeoVector     sysPos,
    const GeoQuat       sysRot,
    const f32           sysScale,
    const TimeDuration  sysTimeRem) {

  const AssetVfxEmitter* emitAsset = &asset->emitters[instance->emitter];
  const TimeDuration     timeRem   = math_min(instance->lifetime - instance->age, sysTimeRem);

  f32 scale = sysScale;
  scale *= math_min(instance->age / (f32)emitAsset->scaleInTime, 1.0f);
  scale *= math_min(timeRem / (f32)emitAsset->scaleOutTime, 1.0f);

  GeoQuat rot = emitAsset->rotation;
  if (emitAsset->facing == AssetVfxFacing_World) {
    rot = geo_quat_mul(sysRot, rot);
  }

  const GeoVector pos = geo_vector_add(sysPos, geo_quat_rotate(sysRot, instance->pos));

  GeoColor color = emitAsset->color;
  color.a *= math_min(instance->age / (f32)emitAsset->fadeInTime, 1.0f);
  color.a *= math_min(timeRem / (f32)emitAsset->fadeOutTime, 1.0f);

  const f32 flipbookFrac  = math_mod_f32(instance->age / (f32)emitAsset->flipbookTime, 1.0f);
  const u32 flipbookIndex = (u32)(flipbookFrac * (f32)emitAsset->flipbookCount);
  diag_assert(flipbookIndex < emitAsset->flipbookCount);

  f32 opacity;
  vfx_blend_mode_apply(color, emitAsset->blend, &color, &opacity);
  vfx_particle_output(
      draw,
      &(VfxParticle){
          .position   = pos,
          .rotation   = rot,
          .flags      = vfx_facing_particle_flags(emitAsset->facing),
          .atlasIndex = instance->atlasBaseIndex + flipbookIndex,
          .sizeX      = scale * emitAsset->sizeX,
          .sizeY      = scale * emitAsset->sizeY,
          .color      = color,
          .opacity    = opacity,
      });
}

ecs_system_define(VfxSystemUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const VfxParticleRendererComp* rend = ecs_view_read_t(globalItr, VfxParticleRendererComp);
  const SceneTimeComp*           time = ecs_view_read_t(globalItr, SceneTimeComp);

  RendDrawComp* draw = ecs_utils_write_t(world, DrawView, vfx_particle_draw(rend), RendDrawComp);
  const AssetAtlasComp* atlas = vfx_atlas(world, vfx_particle_atlas(rend));
  if (!atlas) {
    return; // Atlas hasn't loaded yet.
  }

  EcsIterator* assetItr         = ecs_view_itr(ecs_world_view_t(world, AssetView));
  u32          numAssetRequests = 0;

  vfx_particle_init(draw, atlas);

  EcsView* updateView = ecs_world_view_t(world, UpdateView);
  for (EcsIterator* itr = ecs_view_itr(updateView); ecs_view_walk(itr);) {
    const SceneScaleComp*            scaleComp = ecs_view_read_t(itr, SceneScaleComp);
    const SceneTransformComp*        trans     = ecs_view_read_t(itr, SceneTransformComp);
    const SceneLifetimeDurationComp* lifetime  = ecs_view_read_t(itr, SceneLifetimeDurationComp);
    const SceneVfxComp*              vfx       = ecs_view_read_t(itr, SceneVfxComp);
    VfxStateComp*                    state     = ecs_view_write_t(itr, VfxStateComp);

    const GeoVector    pos     = LIKELY(trans) ? trans->position : geo_vector(0);
    const GeoQuat      rot     = LIKELY(trans) ? trans->rotation : geo_quat_ident;
    const f32          scale   = scaleComp ? scaleComp->scale : 1.0f;
    const TimeDuration timeRem = lifetime ? lifetime->duration : i64_max;

    diag_assert_msg(ecs_entity_valid(vfx->asset), "Vfx system is missing an asset");
    if (!ecs_view_maybe_jump(assetItr, vfx->asset)) {
      if (vfx->asset && ++numAssetRequests < vfx_max_asset_requests) {
        vfx_asset_request(world, vfx->asset);
      }
      continue;
    }
    const AssetVfxComp* asset = ecs_view_read_t(assetItr, AssetVfxComp);

    vfx_system_simulate(state, asset, atlas, time, scale);

    dynarray_for_t(&state->instances, VfxInstance, instance) {
      vfx_instance_output(instance, draw, asset, pos, rot, scale, timeRem);
    }

    state->prevPos = pos;
  }
}

ecs_module_init(vfx_system_module) {
  ecs_register_comp(VfxStateComp, .destructor = ecs_destruct_vfx_state_comp);
  ecs_register_comp(VfxAssetRequestComp, .combinator = ecs_combine_asset_request);

  ecs_register_view(DrawView);
  ecs_register_view(AssetView);
  ecs_register_view(AtlasView);

  ecs_register_system(VfxStateInitSys, ecs_register_view(InitView));
  ecs_register_system(VfxStateDeinitSys, ecs_register_view(DeinitView));

  ecs_register_system(VfxAssetLoadSys, ecs_register_view(LoadView));

  ecs_register_system(
      VfxSystemUpdateSys,
      ecs_register_view(UpdateGlobalView),
      ecs_register_view(UpdateView),
      ecs_view_id(DrawView),
      ecs_view_id(AssetView),
      ecs_view_id(AtlasView));

  ecs_order(VfxSystemUpdateSys, VfxOrder_Update);
}
