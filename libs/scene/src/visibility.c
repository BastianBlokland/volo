#include "core_alloc.h"
#include "ecs_world.h"
#include "geo_vector.h"
#include "log_logger.h"
#include "scene_faction.h"
#include "scene_transform.h"
#include "scene_visibility.h"

#define scene_vision_areas_max 2048

ecs_comp_define(SceneVisibilityEnvComp) {
  GeoVector* visionPositions;    // GeoVector[scene_vision_areas_max]
  f32*       visionSquaredRadii; // (radius * radius)[scene_vision_areas_max]
  u32        visionCount;
};

static void ecs_destruct_visibility_env_comp(void* data) {
  SceneVisibilityEnvComp* env = data;
  alloc_free_array_t(g_alloc_heap, env->visionPositions, scene_vision_areas_max);
  alloc_free_array_t(g_alloc_heap, env->visionSquaredRadii, scene_vision_areas_max);
}

ecs_comp_define_public(SceneVisionComp);

static void visibility_env_create(EcsWorld* world) {
  ecs_world_add_t(
      world,
      ecs_world_global(world),
      SceneVisibilityEnvComp,
      .visionPositions    = alloc_array_t(g_alloc_heap, GeoVector, scene_vision_areas_max),
      .visionSquaredRadii = alloc_array_t(g_alloc_heap, f32, scene_vision_areas_max));
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

ecs_view_define(UpdateGlobalView) { ecs_access_write(SceneVisibilityEnvComp); }

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

  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
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

ecs_module_init(scene_visibility_module) {
  ecs_register_comp(SceneVisibilityEnvComp, .destructor = ecs_destruct_visibility_env_comp);
  ecs_register_comp(SceneVisionComp);

  ecs_register_view(UpdateGlobalView);
  ecs_register_view(VisionEntityView);

  ecs_register_system(
      SceneVisionUpdateSys, ecs_view_id(UpdateGlobalView), ecs_view_id(VisionEntityView));
}

bool scene_visible_pos(
    const SceneVisibilityEnvComp* env, const SceneFaction faction, const GeoVector pos) {
  if (faction != SceneFaction_A) {
    // TODO: Track visiblity for other factions.
    return true;
  }
  // TODO: This could use some kind of acceleration structure.
  for (u32 i = 0; i != env->visionCount; ++i) {
    const GeoVector delta = geo_vector_sub(pos, env->visionPositions[i]);
    if (geo_vector_mag_sqr(delta) <= env->visionSquaredRadii[i]) {
      return true;
    }
  }
  return false;
}
