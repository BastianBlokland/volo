#include "core_alloc.h"
#include "ecs_world.h"
#include "geo_vector.h"
#include "log_logger.h"
#include "scene_faction.h"
#include "scene_transform.h"
#include "scene_visibility.h"

#define scene_vision_areas_max 2048
#define scene_vision_simd_enable 1

#if scene_vision_simd_enable
#include "core_simd.h"
#endif

ecs_comp_define(SceneVisibilityEnvComp) {
  SceneVisibilityFlags flags;
  GeoVector*           visionPositions;    // GeoVector[scene_vision_areas_max]
  f32*                 visionSquaredRadii; // (radius * radius)[scene_vision_areas_max]
  u32                  visionCount;
};

static void ecs_destruct_visibility_env_comp(void* data) {
  SceneVisibilityEnvComp* env = data;
  alloc_free_array_t(g_allocHeap, env->visionPositions, scene_vision_areas_max);
  alloc_free_array_t(g_allocHeap, env->visionSquaredRadii, scene_vision_areas_max);
}

static void ecs_combine_visibility(void* dataA, void* dataB) {
  SceneVisibilityComp* compA = dataA;
  SceneVisibilityComp* compB = dataB;

  compA->visibleToFactionsMask |= compB->visibleToFactionsMask;
}

ecs_comp_define_public(SceneVisibilityComp);
ecs_comp_define_public(SceneVisionComp);

static void visibility_env_create(EcsWorld* world) {
  ecs_world_add_t(
      world,
      ecs_world_global(world),
      SceneVisibilityEnvComp,
      .visionPositions    = alloc_array_t(g_allocHeap, GeoVector, scene_vision_areas_max),
      .visionSquaredRadii = alloc_array_t(g_allocHeap, f32, scene_vision_areas_max));
}

static void visibility_env_clear(SceneVisibilityEnvComp* env) { env->visionCount = 0; }

static void
visibility_env_insert(SceneVisibilityEnvComp* env, const GeoVector pos, const f32 radius) {
  const u32 index = env->visionCount++;
  if (UNLIKELY(index >= scene_vision_areas_max)) {
    log_e("Vision area limit reached", log_param("limit", fmt_int(scene_vision_areas_max)));
    return;
  }
  env->visionPositions[index]    = pos;
  env->visionSquaredRadii[index] = radius * radius;
}

static bool visiblity_env_visible(const SceneVisibilityEnvComp* env, const GeoVector pos) {
  // TODO: This could use some kind of acceleration structure.
#if scene_vision_simd_enable
  const SimdVec posVec = simd_vec_load(pos.comps);
  for (u32 i = 0; i != env->visionCount; ++i) {
    const SimdVec delta        = simd_vec_sub(posVec, simd_vec_load(env->visionPositions[i].comps));
    const SimdVec squaredDist  = simd_vec_dot4(delta, delta);
    const SimdVec squaredRadii = simd_vec_broadcast(env->visionSquaredRadii[i]);
    if (simd_vec_mask_u32(simd_vec_greater(squaredRadii, squaredDist))) {
      return true;
    }
  }
#else
  for (u32 i = 0; i != env->visionCount; ++i) {
    const GeoVector delta = geo_vector_sub(pos, env->visionPositions[i]);
    if (geo_vector_mag_sqr(delta) <= env->visionSquaredRadii[i]) {
      return true;
    }
  }
#endif
  return false;
}

ecs_view_define(VisionUpdateGlobalView) { ecs_access_write(SceneVisibilityEnvComp); }

ecs_view_define(VisionEntityView) {
  ecs_access_read(SceneFactionComp);
  ecs_access_read(SceneTransformComp);
  ecs_access_read(SceneVisionComp);
}

ecs_system_define(SceneVisionUpdateSys) {
  if (!ecs_world_has_t(world, ecs_world_global(world), SceneVisibilityEnvComp)) {
    visibility_env_create(world);
    return;
  }

  EcsView*     globalView = ecs_world_view_t(world, VisionUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }

  SceneVisibilityEnvComp* env = ecs_view_write_t(globalItr, SceneVisibilityEnvComp);
  visibility_env_clear(env);

  EcsView* visionEntities = ecs_world_view_t(world, VisionEntityView);
  for (EcsIterator* itr = ecs_view_itr(visionEntities); ecs_view_walk(itr);) {
    const SceneVisionComp*    vision  = ecs_view_read_t(itr, SceneVisionComp);
    const SceneTransformComp* trans   = ecs_view_read_t(itr, SceneTransformComp);
    const SceneFactionComp*   faction = ecs_view_read_t(itr, SceneFactionComp);

    if (faction->id != SceneFaction_A) {
      // TODO: Track visiblity for other factions.
      continue;
    }

    visibility_env_insert(env, trans->position, vision->radius);
  }
}

ecs_view_define(VisibilityUpdateGlobalView) { ecs_access_read(SceneVisibilityEnvComp); }

ecs_view_define(VisibilityEntityView) {
  ecs_access_read(SceneTransformComp);
  ecs_access_write(SceneVisibilityComp);
}

ecs_system_define(SceneVisibilityUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, VisibilityUpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }

  const SceneVisibilityEnvComp* env = ecs_view_read_t(globalItr, SceneVisibilityEnvComp);

  EcsView* view = ecs_world_view_t(world, VisibilityEntityView);
  for (EcsIterator* itr = ecs_view_itr_step(view, parCount, parIndex); ecs_view_walk(itr);) {
    const SceneTransformComp* trans      = ecs_view_read_t(itr, SceneTransformComp);
    SceneVisibilityComp*      visibility = ecs_view_write_t(itr, SceneVisibilityComp);

    /**
     * NOTE: Only visiblity for faction A is tracked at the moment, the other factions are
     * considered to have full vision.
     */
    visibility->visibleToFactionsMask = (u8)~0;
    if (!visiblity_env_visible(env, trans->position)) {
      visibility->visibleToFactionsMask ^= 1 << SceneFaction_A;
    }
  }
}

ecs_module_init(scene_visibility_module) {
  ecs_register_comp(SceneVisibilityEnvComp, .destructor = ecs_destruct_visibility_env_comp);
  ecs_register_comp(SceneVisibilityComp, .combinator = ecs_combine_visibility);
  ecs_register_comp(SceneVisionComp);

  ecs_register_system(
      SceneVisionUpdateSys,
      ecs_register_view(VisionUpdateGlobalView),
      ecs_register_view(VisionEntityView));

  ecs_register_system(
      SceneVisibilityUpdateSys,
      ecs_register_view(VisibilityUpdateGlobalView),
      ecs_register_view(VisibilityEntityView));

  ecs_parallel(SceneVisibilityUpdateSys, 8);
}

SceneVisibilityFlags scene_visibility_flags(const SceneVisibilityEnvComp* env) {
  return env->flags;
}

void scene_visibility_flags_set(SceneVisibilityEnvComp* env, const SceneVisibilityFlags flags) {
  env->flags |= flags;
}

void scene_visibility_flags_clear(SceneVisibilityEnvComp* env, const SceneVisibilityFlags flags) {
  env->flags &= ~flags;
}

bool scene_visible(const SceneVisibilityComp* visibility, const SceneFaction faction) {
  return (visibility->visibleToFactionsMask & (1 << faction)) != 0;
}

bool scene_visible_for_render(
    const SceneVisibilityEnvComp* env, const SceneVisibilityComp* visibility) {
  if (env->flags & SceneVisibilityFlags_ForceVisibleForRender) {
    return true;
  }
  // TODO: Make the render-faction configurable instead of hardcoding 'A'.
  const SceneFaction renderFaction = SceneFaction_A;
  return (visibility->visibleToFactionsMask & (1 << renderFaction)) != 0;
}

bool scene_visible_pos(
    const SceneVisibilityEnvComp* env, const SceneFaction faction, const GeoVector pos) {
  if (faction != SceneFaction_A) {
    // TODO: Track visiblity for other factions.
    return true;
  }
  return visiblity_env_visible(env, pos);
}
