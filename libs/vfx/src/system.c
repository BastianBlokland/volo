#include "asset_atlas.h"
#include "asset_manager.h"
#include "asset_vfx.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "core_noise.h"
#include "core_rng.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "rend_instance.h"
#include "rend_light.h"
#include "scene_lifetime.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "scene_vfx.h"
#include "vfx_register.h"

#include "particle_internal.h"

#define vfx_system_max_asset_requests 4

typedef struct {
  u8        emitter;
  u16       spriteAtlasBaseIndex;
  f32       lifetimeSec, ageSec;
  f32       scale;
  GeoVector pos;
  GeoQuat   rot;
  GeoVector velo;
} VfxSystemInstance;

typedef struct {
  u32 spawnCount;
} VfxEmitterState;

ecs_comp_define(VfxSystemStateComp) {
  TimeDuration    age;
  VfxEmitterState emitters[asset_vfx_max_emitters];
  DynArray        instances; // VfxSystemInstance[].
};

typedef enum {
  VfxSystemAsset_Acquired  = 1 << 0,
  VfxSystemAsset_Unloading = 1 << 1,
} VfxSystemAssetRequestFlags;

ecs_comp_define(VfxSystemAssetRequestComp) { VfxSystemAssetRequestFlags flags; };

static void ecs_destruct_system_state_comp(void* data) {
  VfxSystemStateComp* comp = data;
  dynarray_destroy(&comp->instances);
}

static void ecs_combine_system_asset_request(void* dataA, void* dataB) {
  VfxSystemAssetRequestComp* compA = dataA;
  VfxSystemAssetRequestComp* compB = dataB;
  compA->flags |= compB->flags;
}

ecs_view_define(ParticleDrawView) {
  ecs_access_with(VfxParticleDrawComp);
  ecs_access_write(RendDrawComp);

  /**
   * Mark the particle draws as explicitly exclusive with scene renderable instance draws. This
   * allows the scheduler to run the vfx draw filling and the instance draw filling in parallel.
   */
  ecs_access_without(RendInstanceDrawComp);
}

ecs_view_define(AtlasView) { ecs_access_read(AssetAtlasComp); }
ecs_view_define(AssetView) { ecs_access_read(AssetVfxComp); }

static const AssetAtlasComp* vfx_atlas(EcsWorld* world, const EcsEntityId atlasEntity) {
  EcsIterator* itr = ecs_view_maybe_at(ecs_world_view_t(world, AtlasView), atlasEntity);
  return LIKELY(itr) ? ecs_view_read_t(itr, AssetAtlasComp) : null;
}

static bool vfx_system_asset_request(EcsWorld* world, const EcsEntityId assetEntity) {
  if (!ecs_world_has_t(world, assetEntity, VfxSystemAssetRequestComp)) {
    ecs_world_add_t(world, assetEntity, VfxSystemAssetRequestComp);
    return true;
  }
  return false;
}

ecs_view_define(InitView) {
  ecs_access_with(SceneVfxSystemComp);
  ecs_access_without(VfxSystemStateComp);
}

ecs_system_define(VfxSystemStateInitSys) {
  EcsView* initView = ecs_world_view_t(world, InitView);
  for (EcsIterator* itr = ecs_view_itr(initView); ecs_view_walk(itr);) {
    ecs_world_add_t(
        world,
        ecs_view_entity(itr),
        VfxSystemStateComp,
        .instances = dynarray_create_t(g_alloc_heap, VfxSystemInstance, 4));
  }
}

ecs_view_define(DeinitView) {
  ecs_access_with(VfxSystemStateComp);
  ecs_access_without(SceneVfxSystemComp);
}

ecs_system_define(VfxSystemStateDeinitSys) {
  EcsView* deinitView = ecs_world_view_t(world, DeinitView);
  for (EcsIterator* itr = ecs_view_itr(deinitView); ecs_view_walk(itr);) {
    ecs_world_remove_t(world, ecs_view_entity(itr), VfxSystemStateComp);
  }
}

ecs_view_define(LoadView) { ecs_access_write(VfxSystemAssetRequestComp); }

ecs_system_define(VfxSystemAssetLoadSys) {
  EcsView* loadView = ecs_world_view_t(world, LoadView);
  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId          entity     = ecs_view_entity(itr);
    VfxSystemAssetRequestComp* request    = ecs_view_write_t(itr, VfxSystemAssetRequestComp);
    const bool                 isLoaded   = ecs_world_has_t(world, entity, AssetLoadedComp);
    const bool                 isFailed   = ecs_world_has_t(world, entity, AssetFailedComp);
    const bool                 hasChanged = ecs_world_has_t(world, entity, AssetChangedComp);

    if (request->flags & VfxSystemAsset_Acquired && (isLoaded || isFailed) && hasChanged) {
      asset_release(world, entity);
      request->flags &= ~VfxSystemAsset_Acquired;
      request->flags |= VfxSystemAsset_Unloading;
    }
    if (request->flags & VfxSystemAsset_Unloading && !isLoaded) {
      request->flags &= ~VfxSystemAsset_Unloading;
    }
    if (!(request->flags & (VfxSystemAsset_Acquired | VfxSystemAsset_Unloading))) {
      asset_acquire(world, entity);
      request->flags |= VfxSystemAsset_Acquired;
    }
  }
}

ecs_view_define(UpdateGlobalView) {
  ecs_access_read(SceneTimeComp);
  ecs_access_read(VfxParticleRendererComp);
  ecs_access_write(RendLightComp);
}

ecs_view_define(UpdateView) {
  ecs_access_maybe_read(SceneLifetimeDurationComp);
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_read(SceneVfxSystemComp);
  ecs_access_write(VfxSystemStateComp);
}

static GeoVector vfx_random_dir_in_cone(const AssetVfxCone* cone) {
  return geo_quat_rotate(cone->rotation, geo_vector_rand_in_cone3(g_rng, cone->angle));
}

static GeoVector vfx_random_in_sphere(const f32 radius) {
  return geo_vector_mul(geo_vector_rand_in_sphere3(g_rng), radius);
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
  case AssetVfxBlend_Additive:
    *outColor   = color;
    *outOpacity = 0.0f;
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

static VfxParticleType vfx_particle_type(const AssetVfxSprite* sprite) {
  return sprite->distortion ? VfxParticleType_Distortion : VfxParticleType_Forward;
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
    VfxSystemStateComp*   state,
    const AssetVfxComp*   asset,
    const AssetAtlasComp* atlas,
    const u8              emitter,
    const VfxTrans*       sysTrans) {

  diag_assert(emitter < asset->emitterCount);
  const AssetVfxEmitter* emitterAsset = &asset->emitters[emitter];

  const StringHash spriteAtlasEntryName  = emitterAsset->sprite.atlasEntry;
  u16              spriteAtlasEntryIndex = sentinel_u16;
  if (spriteAtlasEntryName) {
    const AssetAtlasEntry* atlasEntry = asset_atlas_lookup(atlas, spriteAtlasEntryName);
    if (UNLIKELY(!atlasEntry)) {
      log_e("Vfx atlas entry missing", log_param("entry-hash", fmt_int(spriteAtlasEntryName)));
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
    spriteAtlasEntryIndex = (u16)atlasEntry->atlasIndex;
  }

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

  *dynarray_push_t(&state->instances, VfxSystemInstance) = (VfxSystemInstance){
      .emitter              = emitter,
      .spriteAtlasBaseIndex = spriteAtlasEntryIndex,
      .lifetimeSec          = vfx_sample_range_duration(&emitterAsset->lifetime) / (f32)time_second,
      .scale                = spawnScale,
      .pos                  = geo_vector_add(spawnPos, vfx_random_in_sphere(spawnRadius)),
      .rot                  = vfx_sample_range_rotation(&emitterAsset->rotation),
      .velo                 = geo_vector_mul(spawnDir, spawnSpeed),
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
    VfxSystemStateComp*   state,
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
  VfxSystemInstance* instances = dynarray_begin_t(&state->instances, VfxSystemInstance);
  for (u32 i = (u32)state->instances.size; i-- != 0;) {
    VfxSystemInstance*     instance     = instances + i;
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

static void vfx_instance_output_sprite(
    const VfxSystemInstance* instance,
    RendDrawComp*            particleDraws[VfxParticleType_Count],
    const AssetVfxComp*      asset,
    const VfxTrans*          sysTrans,
    const TimeDuration       sysTimeRem,
    const f32                alpha) {

  if (sentinel_check(instance->spriteAtlasBaseIndex)) {
    return; // Sprites are optional.
  }
  const AssetVfxSpace   space            = asset->emitters[instance->emitter].space;
  const AssetVfxSprite* sprite           = &asset->emitters[instance->emitter].sprite;
  const TimeDuration    instanceAge      = (TimeDuration)time_seconds(instance->ageSec);
  const TimeDuration    instanceLifetime = (TimeDuration)time_seconds(instance->lifetimeSec);
  const TimeDuration    timeRem          = math_min(instanceLifetime - instanceAge, sysTimeRem);

  f32 scale = instance->scale;
  if (space == AssetVfxSpace_Local) {
    scale *= sysTrans->scale;
  }
  scale *= sprite->scaleInTime ? math_min(instanceAge / (f32)sprite->scaleInTime, 1.0f) : 1.0f;
  scale *= sprite->scaleOutTime ? math_min(timeRem / (f32)sprite->scaleOutTime, 1.0f) : 1.0f;

  GeoQuat rot = instance->rot;
  if (sprite->facing == AssetVfxFacing_Local) {
    rot = geo_quat_mul(sysTrans->rot, rot);
  }

  GeoVector pos = instance->pos;
  if (space == AssetVfxSpace_Local) {
    pos = vfx_world_pos(sysTrans, pos);
  }

  GeoColor color = sprite->color;
  color.a *= alpha;
  color.a *= sprite->fadeInTime ? math_min(instanceAge / (f32)sprite->fadeInTime, 1.0f) : 1.0f;
  color.a *= sprite->fadeOutTime ? math_min(timeRem / (f32)sprite->fadeOutTime, 1.0f) : 1.0f;

  const f32 flipbookFrac  = math_mod_f32(instanceAge / (f32)sprite->flipbookTime, 1.0f);
  const u32 flipbookIndex = (u32)(flipbookFrac * (f32)sprite->flipbookCount);
  if (UNLIKELY(flipbookIndex >= sprite->flipbookCount)) {
    return; // NOTE: This can happen momentarily when hot-loading vfx.
  }

  VfxParticleFlags flags = vfx_facing_particle_flags(sprite->facing);
  if (sprite->geometryFade) {
    flags |= VfxParticle_GeometryFade;
  }
  if (sprite->shadowCaster) {
    flags |= VfxParticle_ShadowCaster;
  }
  f32 opacity = 1.0f;
  if (!sprite->distortion) {
    vfx_blend_mode_apply(color, sprite->blend, &color, &opacity);
  }
  vfx_particle_output(
      particleDraws[vfx_particle_type(sprite)],
      &(VfxParticle){
          .position   = pos,
          .rotation   = rot,
          .flags      = flags,
          .atlasIndex = instance->spriteAtlasBaseIndex + flipbookIndex,
          .sizeX      = scale * sprite->sizeX,
          .sizeY      = scale * sprite->sizeY,
          .color      = color,
          .opacity    = opacity,
      });
}

static void vfx_instance_output_light(
    const EcsEntityId        entity,
    const VfxSystemInstance* instance,
    RendLightComp*           lightOutput,
    const AssetVfxComp*      asset,
    const VfxTrans*          sysTrans,
    const TimeDuration       sysTimeRem,
    const f32                alpha) {

  const u32            seed     = ecs_entity_id_index(entity);
  const AssetVfxLight* light    = &asset->emitters[instance->emitter].light;
  GeoColor             radiance = light->radiance;
  if (radiance.a <= f32_epsilon) {
    return; // Lights are optional.
  }
  const TimeDuration  instanceAge      = (TimeDuration)time_seconds(instance->ageSec);
  const TimeDuration  instanceLifetime = (TimeDuration)time_seconds(instance->lifetimeSec);
  const TimeDuration  timeRem          = math_min(instanceLifetime - instanceAge, sysTimeRem);
  const AssetVfxSpace space            = asset->emitters[instance->emitter].space;

  GeoVector pos   = instance->pos;
  f32       scale = instance->scale;
  if (space == AssetVfxSpace_Local) {
    pos = vfx_world_pos(sysTrans, pos);
    scale *= sysTrans->scale;
  }
  radiance.a *= alpha;
  radiance.a *= scale;
  radiance.a *= light->fadeInTime ? math_min(instanceAge / (f32)light->fadeInTime, 1.0f) : 1.0f;
  radiance.a *= light->fadeOutTime ? math_min(timeRem / (f32)light->fadeOutTime, 1.0f) : 1.0f;
  if (light->turbulenceFrequency > 0.0f) {
    // TODO: Make the turbulence scale configurable.
    // TODO: Implement a 2d perlin noise as an optimization.
    radiance.a *= 1.0f - noise_perlin3(instance->ageSec * light->turbulenceFrequency, seed, 0);
  }
  rend_light_point(lightOutput, pos, radiance, light->radius * scale, RendLightFlags_None);
}

ecs_system_define(VfxSystemUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp*           time  = ecs_view_read_t(globalItr, SceneTimeComp);
  const VfxParticleRendererComp* rend  = ecs_view_read_t(globalItr, VfxParticleRendererComp);
  RendLightComp*                 light = ecs_view_write_t(globalItr, RendLightComp);

  const AssetAtlasComp* particleAtlas = vfx_atlas(world, vfx_particle_atlas(rend));
  if (!particleAtlas) {
    return; // Atlas hasn't loaded yet.
  }
  // Initialize the particle draws.
  RendDrawComp* particleDraws[VfxParticleType_Count];
  for (VfxParticleType type = 0; type != VfxParticleType_Count; ++type) {
    const EcsEntityId drawEntity = vfx_particle_draw(rend, type);
    particleDraws[type] = ecs_utils_write_t(world, ParticleDrawView, drawEntity, RendDrawComp);
    vfx_particle_init(particleDraws[type], particleAtlas);
  }

  EcsIterator* assetItr         = ecs_view_itr(ecs_world_view_t(world, AssetView));
  u32          numAssetRequests = 0;

  EcsView* updateView = ecs_world_view_t(world, UpdateView);
  for (EcsIterator* itr = ecs_view_itr(updateView); ecs_view_walk(itr);) {
    const EcsEntityId                entity    = ecs_view_entity(itr);
    const SceneScaleComp*            scaleComp = ecs_view_read_t(itr, SceneScaleComp);
    const SceneTransformComp*        trans     = ecs_view_read_t(itr, SceneTransformComp);
    const SceneLifetimeDurationComp* lifetime  = ecs_view_read_t(itr, SceneLifetimeDurationComp);
    const SceneVfxSystemComp*        vfxSys    = ecs_view_read_t(itr, SceneVfxSystemComp);
    VfxSystemStateComp*              state     = ecs_view_write_t(itr, VfxSystemStateComp);

    const f32 sysAlpha = vfxSys->alpha;
    if (sysAlpha <= f32_epsilon) {
      continue;
    }

    diag_assert_msg(ecs_entity_valid(vfxSys->asset), "Vfx system is missing an asset");
    if (!ecs_view_maybe_jump(assetItr, vfxSys->asset)) {
      if (vfxSys->asset && ++numAssetRequests < vfx_system_max_asset_requests) {
        vfx_system_asset_request(world, vfxSys->asset);
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

    vfx_system_simulate(state, asset, particleAtlas, time, &sysTrans);

    dynarray_for_t(&state->instances, VfxSystemInstance, instance) {
      vfx_instance_output_sprite(instance, particleDraws, asset, &sysTrans, sysTimeRem, sysAlpha);
      vfx_instance_output_light(entity, instance, light, asset, &sysTrans, sysTimeRem, sysAlpha);
    }
  }
}

ecs_module_init(vfx_system_module) {
  ecs_register_comp(VfxSystemStateComp, .destructor = ecs_destruct_system_state_comp);
  ecs_register_comp(VfxSystemAssetRequestComp, .combinator = ecs_combine_system_asset_request);

  ecs_register_view(ParticleDrawView);
  ecs_register_view(AssetView);
  ecs_register_view(AtlasView);

  ecs_register_system(VfxSystemStateInitSys, ecs_register_view(InitView));
  ecs_register_system(VfxSystemStateDeinitSys, ecs_register_view(DeinitView));

  ecs_register_system(VfxSystemAssetLoadSys, ecs_register_view(LoadView));

  ecs_register_system(
      VfxSystemUpdateSys,
      ecs_register_view(UpdateGlobalView),
      ecs_register_view(UpdateView),
      ecs_view_id(ParticleDrawView),
      ecs_view_id(AssetView),
      ecs_view_id(AtlasView));

  ecs_order(VfxSystemUpdateSys, VfxOrder_Update);
}
