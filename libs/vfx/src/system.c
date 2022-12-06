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
  u8        emitter;
  u16       atlasBaseIndex;
  f32       lifetimeSec, ageSec;
  f32       scale;
  GeoVector pos;
  GeoQuat   rot;
  GeoVector velo;
} VfxInstance;

typedef struct {
  u32 spawnCount;
} VfxEmitterState;

ecs_comp_define(VfxStateComp) {
  TimeDuration    age;
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
  ecs_access_with(SceneVfxComp);
  ecs_access_without(VfxStateComp);
}

ecs_system_define(VfxStateInitSys) {
  EcsView* initView = ecs_world_view_t(world, InitView);
  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    const EcsEntityId e = ecs_view_entity(itr);

    ecs_world_add_t(
        world, e, VfxStateComp, .instances = dynarray_create_t(g_alloc_heap, VfxInstance, 4));
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

static GeoVector vfx_random_dir() {
  /**
   * Generate a random point on a unit sphere (radius of 1, aka a direction vector).
   */
Retry:;
  const RngGaussPairF32 gauss1 = rng_sample_gauss_f32(g_rng);
  const RngGaussPairF32 gauss2 = rng_sample_gauss_f32(g_rng);
  const GeoVector       vec    = {.x = gauss1.a, .y = gauss1.b, .z = gauss2.a};
  const f32             magSqr = geo_vector_mag_sqr(vec);
  if (UNLIKELY(magSqr <= f32_epsilon)) {
    goto Retry; // Reject zero vectors (rare case).
  }
  return geo_vector_div(vec, math_sqrt_f32(magSqr));
}

static GeoVector vfx_random_in_sphere(const f32 radius) {
  /**
   * Generate a random point inside a sphere.
   * NOTE: Cube-root as the area increases cubicly as you get further from the center.
   */
  const GeoVector dir = vfx_random_dir();
  return geo_vector_mul(dir, radius * math_cbrt_f32(rng_sample_f32(g_rng)));
}

static f32 vfx_sample_range_scalar(const AssetVfxRangeScalar* scalar) {
  return rng_sample_range(g_rng, scalar->min, scalar->max);
}

static TimeDuration vfx_sample_range_duration(const AssetVfxRangeDuration* duration) {
  return (TimeDuration)rng_sample_range(g_rng, duration->min, duration->max);
}

static GeoQuat vfx_sample_range_rotation(const AssetVfxRangeRotation* rotation) {
  const f32       rand              = rng_sample_f32(g_rng);
  const GeoVector randomEulerAngles = geo_vector_mul(rotation->randomEulerAngles, rand);
  return geo_quat_mul(rotation->base, geo_quat_from_euler(randomEulerAngles));
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
  case AssetVfxBlend_AlphaDouble:
    *outColor   = color;
    *outOpacity = color.a * 2;
    return;
  case AssetVfxBlend_AlphaQuad:
    *outColor   = color;
    *outOpacity = color.a * 4;
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
  case AssetVfxFacing_Local:
    return 0;
  case AssetVfxFacing_BillboardSphere:
    return VfxParticle_BillboardSphere;
  case AssetVfxFacing_BillboardCylinder:
    return VfxParticle_BillboardCylinder;
  }
  UNREACHABLE
}

typedef struct {
  GeoVector pos;
  GeoQuat   rot;
  f32       scale;
} VfxTrans;

static GeoVector vfx_world_pos(const VfxTrans* t, const GeoVector pos) {
  return geo_vector_add(t->pos, geo_quat_rotate(t->rot, geo_vector_mul(pos, t->scale)));
}

static GeoVector vfx_world_dir(const VfxTrans* t, const GeoVector dir) {
  return geo_quat_rotate(t->rot, dir);
}

static void vfx_system_spawn(
    VfxStateComp*         state,
    const AssetVfxComp*   asset,
    const AssetAtlasComp* atlas,
    const u8              emitter,
    const VfxTrans*       sysTrans) {

  diag_assert(emitter < asset->emitterCount);
  const AssetVfxEmitter* emitterAsset = &asset->emitters[emitter];

  const StringHash       atlasEntryName = emitterAsset->sprite.atlasEntry;
  const AssetAtlasEntry* atlasEntry     = asset_atlas_lookup(atlas, atlasEntryName);
  if (UNLIKELY(!atlasEntry)) {
    log_e("Vfx atlas entry missing", log_param("entry-hash", fmt_int(atlasEntryName)));
    return;
  }
  if (UNLIKELY(atlasEntry->atlasIndex + emitterAsset->sprite.flipbookCount > atlas->entryCount)) {
    log_e(
        "Vfx atlas has not enough entries for flipbook",
        log_param("atlas-entry-count", fmt_int(atlas->entryCount)),
        log_param("flipbook-count", fmt_int(emitterAsset->sprite.flipbookCount)));
    return;
  }

  diag_assert_msg(atlasEntry->atlasIndex <= u16_max, "Atlas index exceeds limit");

  GeoVector spawnPos    = emitterAsset->cone.position;
  f32       spawnRadius = emitterAsset->cone.radius;
  GeoVector spawnDir    = vfx_random_dir_in_cone(&emitterAsset->cone);
  f32       spawnScale  = vfx_sample_range_scalar(&emitterAsset->scale);
  f32       spawnSpeed  = vfx_sample_range_scalar(&emitterAsset->speed);
  if (emitterAsset->space == AssetVfxSpace_World) {
    spawnPos = vfx_world_pos(sysTrans, spawnPos);
    spawnRadius *= sysTrans->scale;
    spawnDir = vfx_world_dir(sysTrans, spawnDir);
    spawnScale *= sysTrans->scale;
    spawnSpeed *= sysTrans->scale;
  }

  *dynarray_push_t(&state->instances, VfxInstance) = (VfxInstance){
      .emitter        = emitter,
      .atlasBaseIndex = (u16)atlasEntry->atlasIndex,
      .lifetimeSec    = vfx_sample_range_duration(&emitterAsset->lifetime) / (f32)time_second,
      .scale          = spawnScale,
      .pos            = geo_vector_add(spawnPos, vfx_random_in_sphere(spawnRadius)),
      .rot            = vfx_sample_range_rotation(&emitterAsset->rotation),
      .velo           = geo_vector_mul(spawnDir, spawnSpeed),
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
    const VfxTrans*       sysTrans) {

  const f32 deltaSec = scene_delta_seconds(time);

  // Update shared state.
  state->age += time->delta;

  // Update emitters.
  for (u32 emitter = 0; emitter != asset->emitterCount; ++emitter) {
    VfxEmitterState*       emitterState = &state->emitters[emitter];
    const AssetVfxEmitter* emitterAsset = &asset->emitters[emitter];

    const u32 count = vfx_emitter_count(emitterAsset, state->age);
    for (; emitterState->spawnCount < count; ++emitterState->spawnCount) {
      vfx_system_spawn(state, asset, atlas, emitter, sysTrans);
    }
  }

  // Update instances.
  VfxInstance* instances = dynarray_begin_t(&state->instances, VfxInstance);
  for (u32 i = (u32)state->instances.size; i-- != 0;) {
    VfxInstance*           instance     = instances + i;
    const AssetVfxEmitter* emitterAsset = &asset->emitters[instance->emitter];

    // Apply force.
    instance->velo = geo_vector_add(instance->velo, geo_vector_mul(emitterAsset->force, deltaSec));

    // Apply expanding.
    instance->scale += emitterAsset->expandForce * deltaSec;

    // Apply movement.
    instance->pos = geo_vector_add(instance->pos, geo_vector_mul(instance->velo, deltaSec));

    // Update age and destruct if too old.
    if ((instance->ageSec += deltaSec) > instance->lifetimeSec) {
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
    const VfxTrans*     sysTrans,
    const TimeDuration  sysTimeRem) {

  const AssetVfxSpace   space            = asset->emitters[instance->emitter].space;
  const AssetVfxSprite* sprite           = &asset->emitters[instance->emitter].sprite;
  const TimeDuration    instanceAge      = (TimeDuration)time_seconds(instance->ageSec);
  const TimeDuration    instanceLifetime = (TimeDuration)time_seconds(instance->lifetimeSec);
  const TimeDuration    timeRem          = math_min(instanceLifetime - instanceAge, sysTimeRem);

  f32 scale = instance->scale;
  if (space == AssetVfxSpace_Local) {
    scale *= sysTrans->scale;
  }
  scale *= math_min(instanceAge / (f32)sprite->scaleInTime, 1.0f);
  scale *= math_min(timeRem / (f32)sprite->scaleOutTime, 1.0f);

  GeoQuat rot = instance->rot;
  if (sprite->facing == AssetVfxFacing_Local) {
    rot = geo_quat_mul(sysTrans->rot, rot);
  }

  GeoVector pos = instance->pos;
  if (space == AssetVfxSpace_Local) {
    pos = vfx_world_pos(sysTrans, pos);
  }

  GeoColor color = sprite->color;
  color.a *= math_min(instanceAge / (f32)sprite->fadeInTime, 1.0f);
  color.a *= math_min(timeRem / (f32)sprite->fadeOutTime, 1.0f);

  const f32 flipbookFrac  = math_mod_f32(instanceAge / (f32)sprite->flipbookTime, 1.0f);
  const u32 flipbookIndex = (u32)(flipbookFrac * (f32)sprite->flipbookCount);
  if (UNLIKELY(flipbookIndex >= sprite->flipbookCount)) {
    return; // NOTE: This can happen momentarily when hot-loading vfx.
  }

  f32 opacity;
  vfx_blend_mode_apply(color, sprite->blend, &color, &opacity);
  vfx_particle_output(
      draw,
      &(VfxParticle){
          .position   = pos,
          .rotation   = rot,
          .flags      = vfx_facing_particle_flags(sprite->facing),
          .atlasIndex = instance->atlasBaseIndex + flipbookIndex,
          .sizeX      = scale * sprite->sizeX,
          .sizeY      = scale * sprite->sizeY,
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

    diag_assert_msg(ecs_entity_valid(vfx->asset), "Vfx system is missing an asset");
    if (!ecs_view_maybe_jump(assetItr, vfx->asset)) {
      if (vfx->asset && ++numAssetRequests < vfx_max_asset_requests) {
        vfx_asset_request(world, vfx->asset);
      }
      continue;
    }
    const AssetVfxComp* asset = ecs_view_read_t(assetItr, AssetVfxComp);

    VfxTrans sysTrans = {
        .pos   = LIKELY(trans) ? trans->position : geo_vector(0),
        .rot   = geo_quat_ident,
        .scale = scaleComp ? scaleComp->scale : 1.0f,
    };
    if (!(asset->flags & AssetVfx_IgnoreTransformRotation)) {
      sysTrans.rot = LIKELY(trans) ? trans->rotation : geo_quat_ident;
    }

    const TimeDuration sysTimeRem = lifetime ? lifetime->duration : i64_max;

    vfx_system_simulate(state, asset, atlas, time, &sysTrans);

    dynarray_for_t(&state->instances, VfxInstance, instance) {
      vfx_instance_output(instance, draw, asset, &sysTrans, sysTimeRem);
    }
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
