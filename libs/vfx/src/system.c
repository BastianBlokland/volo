#include "asset_atlas.h"
#include "asset_manager.h"
#include "asset_vfx.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "core_noise.h"
#include "core_rng.h"
#include "ecs_utils.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "rend_light.h"
#include "scene_lifetime.h"
#include "scene_tag.h"
#include "scene_time.h"
#include "scene_transform.h"
#include "scene_vfx.h"
#include "scene_visibility.h"
#include "vfx_register.h"

#include "atlas_internal.h"
#include "draw_internal.h"
#include "particle_internal.h"

#define vfx_system_max_asset_requests 4

typedef enum {
  VfxLoad_Acquired  = 1 << 0,
  VfxLoad_Unloading = 1 << 1,
} VfxLoadFlags;

typedef struct {
  u8        emitter;
  u8        alpha; // Normalized.
  u16       spriteAtlasBaseIndex;
  f32       lifetimeSec, ageSec;
  f32       scale;
  GeoVector pos;
  GeoQuat   rot;
  GeoVector velo;
} VfxSystemInstance;

ASSERT(sizeof(VfxSystemInstance) <= 64, "Instance should fit in a single cacheline on x86");

typedef struct {
  u16 spawnCount;
} VfxEmitterState;

ecs_comp_define(VfxSystemStateComp) {
  TimeDuration    age, emitAge;
  u16             assetVersion;
  VfxEmitterState emitters[asset_vfx_max_emitters];
  DynArray        instances; // VfxSystemInstance[].
};

ecs_comp_define(VfxSystemAssetComp) {
  VfxLoadFlags loadFlags : 16;
  u16          version;
};

static void ecs_destruct_system_state_comp(void* data) {
  VfxSystemStateComp* comp = data;
  dynarray_destroy(&comp->instances);
}

static void ecs_combine_system_asset(void* dataA, void* dataB) {
  VfxSystemAssetComp* compA = dataA;
  VfxSystemAssetComp* compB = dataB;
  compA->loadFlags |= compB->loadFlags;
}

ecs_view_define(ParticleDrawView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // This is the only module accessing particle draws.
  ecs_access_write(RendDrawComp);
}

ecs_view_define(AtlasView) { ecs_access_read(AssetAtlasComp); }

ecs_view_define(AssetView) {
  ecs_access_read(VfxSystemAssetComp);
  ecs_access_read(AssetVfxComp);
}

INLINE_HINT static f32 vfx_mod1(const f32 val) { return val - (u32)val; }

/**
 * Approximate the given base to the power of exp.
 * NOTE: Assumes the host system is using little-endian byte-order and 2's complement integers.
 *
 * Implementation based on:
 * https://martin.ankerl.com/2007/10/04/optimized-pow-approximation-for-java-and-c-c/
 * https://github.com/ekmett/approximate/blob/master/cbits/fast.c
 */
INLINE_HINT static f32 vfx_pow_approx(const f32 base, const f32 exp) {
  union {
    f32 valF32;
    u32 valU32;
  } conv      = {base};
  conv.valU32 = (u32)(exp * (conv.valU32 - u32_lit(1064631197)) + 1065353216.0f);
  return conv.valF32;
}

static f32 vfx_time_to_seconds(const TimeDuration dur) {
  static const f64 g_toSecMul = 1.0 / (f64)time_second;
  // NOTE: Potentially can be done in 32 bit but with nano-seconds its at the edge of f32 precision.
  return (f32)((f64)dur * g_toSecMul);
}

static const AssetAtlasComp* vfx_atlas_particle(EcsWorld* world, const VfxAtlasManagerComp* man) {
  const EcsEntityId atlasEntity = vfx_atlas_entity(man, VfxAtlasType_Particle);
  EcsIterator*      itr = ecs_view_maybe_at(ecs_world_view_t(world, AtlasView), atlasEntity);
  return LIKELY(itr) ? ecs_view_read_t(itr, AssetAtlasComp) : null;
}

static bool vfx_system_asset_valid(EcsWorld* world, const EcsEntityId assetEntity) {
  return ecs_world_exists(world, assetEntity) && ecs_world_has_t(world, assetEntity, AssetComp);
}

static bool vfx_system_asset_request(EcsWorld* world, const EcsEntityId assetEntity) {
  if (!ecs_world_has_t(world, assetEntity, VfxSystemAssetComp)) {
    ecs_world_add_t(world, assetEntity, VfxSystemAssetComp);
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
        .instances = dynarray_create_t(g_allocHeap, VfxSystemInstance, 4));
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

ecs_view_define(LoadView) { ecs_access_write(VfxSystemAssetComp); }

ecs_system_define(VfxSystemAssetLoadSys) {
  EcsView* loadView = ecs_world_view_t(world, LoadView);
  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId   entity     = ecs_view_entity(itr);
    VfxSystemAssetComp* request    = ecs_view_write_t(itr, VfxSystemAssetComp);
    const bool          isLoaded   = ecs_world_has_t(world, entity, AssetLoadedComp);
    const bool          isFailed   = ecs_world_has_t(world, entity, AssetFailedComp);
    const bool          hasChanged = ecs_world_has_t(world, entity, AssetChangedComp);

    if (request->loadFlags & VfxLoad_Acquired && (isLoaded || isFailed) && hasChanged) {
      asset_release(world, entity);
      request->loadFlags &= ~VfxLoad_Acquired;
      request->loadFlags |= VfxLoad_Unloading;
    }
    if (request->loadFlags & VfxLoad_Unloading && !isLoaded) {
      request->loadFlags &= ~VfxLoad_Unloading;
    }
    if (!(request->loadFlags & (VfxLoad_Acquired | VfxLoad_Unloading))) {
      asset_acquire(world, entity);
      request->loadFlags |= VfxLoad_Acquired;
      ++request->version;
    }
  }
}

ecs_view_define(UpdateGlobalView) {
  ecs_access_read(SceneTimeComp);
  ecs_access_read(SceneVisibilityEnvComp);
  ecs_access_read(VfxAtlasManagerComp);
  ecs_access_read(VfxDrawManagerComp);
  ecs_access_write(RendLightComp);
}

ecs_view_define(UpdateView) {
  ecs_access_maybe_read(SceneLifetimeDurationComp);
  ecs_access_maybe_read(SceneScaleComp);
  ecs_access_maybe_read(SceneTagComp);
  ecs_access_maybe_read(SceneTransformComp);
  ecs_access_maybe_read(SceneVisibilityComp);
  ecs_access_read(SceneVfxSystemComp);
  ecs_access_write(VfxSystemStateComp);
}

INLINE_HINT static f32 vfx_instance_alpha(const VfxSystemInstance* instance) {
  static const f32 g_u8MaxInv = 1.0f / u8_max;
  return (f32)instance->alpha * g_u8MaxInv;
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

static VfxDrawType vfx_sprite_draw_type(const AssetVfxSprite* sprite) {
  return sprite->distortion ? VfxDrawType_ParticleDistortion : VfxDrawType_ParticleForward;
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
    VfxSystemStateComp*       state,
    const AssetVfxComp*       asset,
    const AssetAtlasComp*     atlas,
    const u8                  emitter,
    const SceneVfxSystemComp* sysCfg,
    const VfxTrans*           sysTrans) {

  diag_assert(emitter < asset->emitterCount);
  const AssetVfxEmitter* emitterAsset = &asset->emitters[emitter];

  const StringHash spriteAtlasEntryName  = emitterAsset->sprite.atlasEntry;
  u16              spriteAtlasEntryIndex = sentinel_u16;
  if (spriteAtlasEntryName) {
    const AssetAtlasEntry* atlasEntry = asset_atlas_lookup(atlas, spriteAtlasEntryName);
    if (UNLIKELY(!atlasEntry)) {
      log_e(
          "Vfx particle atlas entry missing",
          log_param("entry-hash", fmt_int(spriteAtlasEntryName)));
      return;
    }
    if (UNLIKELY(atlasEntry->atlasIndex + emitterAsset->sprite.flipbookCount > atlas->entryCount)) {
      log_e(
          "Vfx particle atlas has not enough entries for flipbook",
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
  f32       spawnAlpha  = 1.0f;
  if (emitterAsset->space == AssetVfxSpace_World) {
    spawnPos = vfx_world_pos(sysTrans, spawnPos);
    spawnRadius *= sysTrans->scale;
    spawnDir = vfx_world_dir(sysTrans, spawnDir);
    spawnScale *= sysTrans->scale;
    spawnSpeed *= sysTrans->scale;
    spawnAlpha *= sysCfg->alpha;
  }

  *dynarray_push_t(&state->instances, VfxSystemInstance) = (VfxSystemInstance){
      .emitter              = emitter,
      .alpha                = (u8)(math_clamp_f32(spawnAlpha, 0.0f, 1.0f) * u8_max),
      .spriteAtlasBaseIndex = spriteAtlasEntryIndex,
      .lifetimeSec = vfx_time_to_seconds(vfx_sample_range_duration(&emitterAsset->lifetime)),
      .scale       = spawnScale,
      .pos         = geo_vector_add(spawnPos, vfx_random_in_sphere(spawnRadius)),
      .rot         = vfx_sample_range_rotation(&emitterAsset->rotation),
      .velo        = geo_vector_mul(spawnDir, spawnSpeed),
  };
}

static u16 vfx_emitter_count(const AssetVfxEmitter* emitterAsset, const TimeDuration age) {
  if (emitterAsset->interval) {
    const u16 maxCount = emitterAsset->count ? emitterAsset->count : u16_max;
    return math_min((u16)(age / emitterAsset->interval), maxCount);
  }
  return math_max(1, emitterAsset->count);
}

static void vfx_system_reset(VfxSystemStateComp* state) {
  /**
   * Reset the spawn-state so that instances will be re-spawned.
   */
  state->emitAge = 0;
  array_for_t(state->emitters, VfxEmitterState, emitter) { emitter->spawnCount = 0; }

  /**
   * Delete instances with very long (possibly infinite) lifetimes.
   * NOTE: Alternatively we could simply delete all instances, however when working on a particle
   * system with fast dying particles (for example fire) its less intrusive to simply let those old
   * instances die on their own.
   */
  for (u32 i = (u32)state->instances.size; i-- != 0;) {
    const VfxSystemInstance* instance = dynarray_at_t(&state->instances, i, VfxSystemInstance);
    if (instance->lifetimeSec > 60.0f) {
      dynarray_remove_unordered(&state->instances, i, 1);
    }
  }
}

static void vfx_system_simulate(
    VfxSystemStateComp*       state,
    const AssetVfxComp*       asset,
    const AssetAtlasComp*     atlas,
    const SceneTimeComp*      time,
    const SceneTags           tags,
    const SceneVfxSystemComp* sysCfg,
    const VfxTrans*           sysTrans) {

  const f32 deltaSec = vfx_time_to_seconds(time->delta);

  // Update shared state.
  state->age += time->delta;
  if (tags & SceneTags_Emit) {
    state->emitAge += (TimeDuration)(time->delta * sysCfg->emitMultiplier);
  }

  // Update emitters.
  for (u32 emitter = 0; emitter != asset->emitterCount; ++emitter) {
    VfxEmitterState*       emitterState = &state->emitters[emitter];
    const AssetVfxEmitter* emitterAsset = &asset->emitters[emitter];

    const u16 count = vfx_emitter_count(emitterAsset, state->emitAge);
    for (; emitterState->spawnCount < count; ++emitterState->spawnCount) {
      vfx_system_spawn(state, asset, atlas, emitter, sysCfg, sysTrans);
    }
  }

  // Update instances.
  VfxSystemInstance* instances = dynarray_begin_t(&state->instances, VfxSystemInstance);
  for (u32 i = (u32)state->instances.size; i-- != 0;) {
    VfxSystemInstance*     inst         = instances + i;
    const AssetVfxEmitter* emitterAsset = &asset->emitters[inst->emitter];

    // Apply force.
    inst->velo = geo_vector_add(inst->velo, geo_vector_mul(emitterAsset->force, deltaSec));

    // Apply friction.
    inst->velo = geo_vector_mul(inst->velo, vfx_pow_approx(emitterAsset->friction, deltaSec));

    // Apply expanding.
    inst->scale += emitterAsset->expandForce * deltaSec;

    // Apply movement.
    inst->pos = geo_vector_add(inst->pos, geo_vector_mul(inst->velo, deltaSec));

    // Update age and destruct if too old.
    if ((inst->ageSec += deltaSec) > inst->lifetimeSec) {
      goto Destruct;
    }
    continue;

  Destruct:
    dynarray_remove_unordered(&state->instances, i, 1);
  }
}

static void vfx_instance_output_sprite(
    const VfxSystemInstance*  instance,
    RendDrawComp*             draws[VfxDrawType_Count],
    const AssetVfxComp*       asset,
    const SceneVfxSystemComp* sysCfg,
    const VfxTrans*           sysTrans,
    const f32                 sysTimeRemSec) {

  if (sentinel_check(instance->spriteAtlasBaseIndex)) {
    return; // Sprites are optional.
  }
  const AssetVfxSpace   space  = asset->emitters[instance->emitter].space;
  const AssetVfxSprite* sprite = &asset->emitters[instance->emitter].sprite;
  const f32 timeRemSec         = math_min(instance->lifetimeSec - instance->ageSec, sysTimeRemSec);

  f32 scale = instance->scale;
  if (space == AssetVfxSpace_Local) {
    scale *= sysTrans->scale;
  }
  scale *= math_min(instance->ageSec * sprite->scaleInTimeInv, 1.0f);
  scale *= math_min(timeRemSec * sprite->scaleOutTimeInv, 1.0f);

  GeoQuat rot = instance->rot;
  if (sprite->facing == AssetVfxFacing_Local) {
    rot = geo_quat_mul(sysTrans->rot, rot);
  }

  GeoVector pos   = instance->pos;
  GeoColor  color = sprite->color;
  color.a *= vfx_instance_alpha(instance);
  if (space == AssetVfxSpace_Local) {
    pos = vfx_world_pos(sysTrans, pos);
    color.a *= sysCfg->alpha;
  }
  color.a *= math_min(instance->ageSec * sprite->fadeInTimeInv, 1.0f);
  color.a *= math_min(timeRemSec * sprite->fadeOutTimeInv, 1.0f);

  const f32 flipbookFrac  = vfx_mod1(instance->ageSec * sprite->flipbookTimeInv);
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
      draws[vfx_sprite_draw_type(sprite)],
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
    const EcsEntityId         entity,
    const VfxSystemInstance*  instance,
    RendLightComp*            lightOutput,
    const AssetVfxComp*       asset,
    const SceneVfxSystemComp* sysCfg,
    const VfxTrans*           sysTrans,
    const f32                 sysTimeRemSec) {

  const u32            seed     = ecs_entity_id_index(entity);
  const AssetVfxLight* light    = &asset->emitters[instance->emitter].light;
  GeoColor             radiance = light->radiance;
  radiance.a *= vfx_instance_alpha(instance);
  if (radiance.a <= f32_epsilon) {
    return;
  }
  const f32 timeRemSec      = math_min(instance->lifetimeSec - instance->ageSec, sysTimeRemSec);
  const AssetVfxSpace space = asset->emitters[instance->emitter].space;

  GeoVector pos   = instance->pos;
  f32       scale = instance->scale;
  if (space == AssetVfxSpace_Local) {
    pos = vfx_world_pos(sysTrans, pos);
    scale *= sysTrans->scale;
    radiance.a *= sysCfg->alpha;
  }
  radiance.a *= scale;
  radiance.a *= math_min(instance->ageSec * light->fadeInTimeInv, 1.0f);
  radiance.a *= math_min(timeRemSec * light->fadeOutTimeInv, 1.0f);
  if (light->turbulenceFrequency > 0.0f) {
    // TODO: Make the turbulence scale configurable.
    // TODO: Implement a 2d perlin noise as an optimization.
    radiance.a *= 1.0f - noise_perlin3(instance->ageSec * light->turbulenceFrequency, seed, 0);
  }
  rend_light_point(lightOutput, pos, radiance, light->radius * scale, RendLightFlags_None);
}

ecs_system_define(VfxSystemRenderSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  const SceneTimeComp*          time         = ecs_view_read_t(globalItr, SceneTimeComp);
  const VfxDrawManagerComp*     drawManager  = ecs_view_read_t(globalItr, VfxDrawManagerComp);
  const VfxAtlasManagerComp*    atlasManager = ecs_view_read_t(globalItr, VfxAtlasManagerComp);
  const SceneVisibilityEnvComp* visEnv       = ecs_view_read_t(globalItr, SceneVisibilityEnvComp);
  RendLightComp*                light        = ecs_view_write_t(globalItr, RendLightComp);

  const AssetAtlasComp* particleAtlas = vfx_atlas_particle(world, atlasManager);
  if (!particleAtlas) {
    return; // Atlas hasn't loaded yet.
  }
  // Initialize the particle draws.
  RendDrawComp* draws[VfxDrawType_Count] = {null};
  for (VfxDrawType type = 0; type != VfxDrawType_Count; ++type) {
    if (type == VfxDrawType_ParticleForward || type == VfxDrawType_ParticleDistortion) {
      const EcsEntityId drawEntity = vfx_draw_entity(drawManager, type);
      draws[type] = ecs_utils_write_t(world, ParticleDrawView, drawEntity, RendDrawComp);
      vfx_particle_init(draws[type], particleAtlas);
    }
  }

  EcsIterator* assetItr         = ecs_view_itr(ecs_world_view_t(world, AssetView));
  u32          numAssetRequests = 0;

  EcsView* updateView = ecs_world_view_t(world, UpdateView);
  for (EcsIterator* itr = ecs_view_itr(updateView); ecs_view_walk(itr);) {
    const EcsEntityId                entity    = ecs_view_entity(itr);
    const SceneScaleComp*            scaleComp = ecs_view_read_t(itr, SceneScaleComp);
    const SceneTransformComp*        trans     = ecs_view_read_t(itr, SceneTransformComp);
    const SceneLifetimeDurationComp* lifetime  = ecs_view_read_t(itr, SceneLifetimeDurationComp);
    const SceneVfxSystemComp*        sysCfg    = ecs_view_read_t(itr, SceneVfxSystemComp);
    const SceneVisibilityComp*       sysVis    = ecs_view_read_t(itr, SceneVisibilityComp);
    const SceneTagComp*              tagComp   = ecs_view_read_t(itr, SceneTagComp);
    VfxSystemStateComp*              state     = ecs_view_write_t(itr, VfxSystemStateComp);

    const SceneTags sysTags    = tagComp ? tagComp->tags : SceneTags_Default;
    const bool      sysVisible = !sysVis || scene_visible_for_render(visEnv, sysVis);

    diag_assert_msg(ecs_entity_valid(sysCfg->asset), "Vfx system is missing an asset");
    if (!ecs_view_maybe_jump(assetItr, sysCfg->asset)) {
      if (UNLIKELY(!vfx_system_asset_valid(world, sysCfg->asset))) {
        log_e("Invalid vfx-system asset entity");
        continue;
      } else if (UNLIKELY(ecs_world_has_t(world, sysCfg->asset, AssetFailedComp))) {
        log_e("Failed to acquire vfx-system asset");
        continue;
      } else if (UNLIKELY(ecs_world_has_t(world, sysCfg->asset, AssetLoadedComp))) {
        log_e("Acquired asset was not a vfx-system");
        continue;
      }
      if (sysCfg->asset && ++numAssetRequests < vfx_system_max_asset_requests) {
        vfx_system_asset_request(world, sysCfg->asset);
      }
      continue;
    }
    const VfxSystemAssetComp* assetRequest = ecs_view_read_t(assetItr, VfxSystemAssetComp);
    const AssetVfxComp*       asset        = ecs_view_read_t(assetItr, AssetVfxComp);

    if (UNLIKELY(state->assetVersion != assetRequest->version)) {
      if (state->assetVersion) {
        vfx_system_reset(state); // Reset the state after hot-loading the asset.
      }
      state->assetVersion = assetRequest->version;
      continue; // Skip the system this frame; This gives time for the old asset to be unloaded.
    }

    VfxTrans sysTrans = {
        .pos   = LIKELY(trans) ? trans->position : geo_vector(0),
        .rot   = geo_quat_ident,
        .scale = scaleComp ? scaleComp->scale : 1.0f,
    };
    if (!(asset->flags & AssetVfx_IgnoreTransformRotation)) {
      sysTrans.rot = LIKELY(trans) ? trans->rotation : geo_quat_ident;
    }

    vfx_system_simulate(state, asset, particleAtlas, time, sysTags, sysCfg, &sysTrans);

    if (sysVisible) {
      const f32 sysTimeRemSec = lifetime ? vfx_time_to_seconds(lifetime->duration) : f32_max;
      dynarray_for_t(&state->instances, VfxSystemInstance, instance) {
        vfx_instance_output_sprite(instance, draws, asset, sysCfg, &sysTrans, sysTimeRemSec);
        vfx_instance_output_light(entity, instance, light, asset, sysCfg, &sysTrans, sysTimeRemSec);
      }
    }
  }
}

ecs_module_init(vfx_system_module) {
  ecs_register_comp(VfxSystemStateComp, .destructor = ecs_destruct_system_state_comp);
  ecs_register_comp(VfxSystemAssetComp, .combinator = ecs_combine_system_asset);

  ecs_register_view(ParticleDrawView);
  ecs_register_view(AssetView);
  ecs_register_view(AtlasView);

  ecs_register_system(VfxSystemStateInitSys, ecs_register_view(InitView));
  ecs_register_system(VfxSystemStateDeinitSys, ecs_register_view(DeinitView));

  ecs_register_system(VfxSystemAssetLoadSys, ecs_register_view(LoadView));

  ecs_register_system(
      VfxSystemRenderSys,
      ecs_register_view(UpdateGlobalView),
      ecs_register_view(UpdateView),
      ecs_view_id(ParticleDrawView),
      ecs_view_id(AssetView),
      ecs_view_id(AtlasView));

  ecs_order(VfxSystemRenderSys, VfxOrder_Render);
}
