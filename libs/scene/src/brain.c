#include "ai_blackboard.h"
#include "ai_eval.h"
#include "asset_behavior.h"
#include "asset_manager.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_brain.h"

#define scene_brain_max_behavior_loads 8

typedef enum {
  SceneBehavior_ResourceAcquired  = 1 << 0,
  SceneBehavior_ResourceUnloading = 1 << 1,
} SceneBehaviorFlags;

ecs_comp_define(SceneBrainComp) {
  AiBlackboard* blackboard;
  EcsEntityId   behaviorAsset;
};

ecs_comp_define(SceneBehaviorResourceComp) { SceneBehaviorFlags flags; };

static void ecs_destruct_brain_comp(void* data) {
  SceneBrainComp* brain = data;
  ai_blackboard_destroy(brain->blackboard);
}

static void ecs_combine_behavior_resource(void* dataA, void* dataB) {
  SceneBehaviorResourceComp* a = dataA;
  SceneBehaviorResourceComp* b = dataB;
  a->flags |= b->flags;
}

ecs_view_define(BrainEntityView) { ecs_access_write(SceneBrainComp); }
ecs_view_define(BehaviorView) { ecs_access_read(AssetBehaviorComp); }
ecs_view_define(BehaviorLoadView) { ecs_access_write(SceneBehaviorResourceComp); }

ecs_system_define(SceneBehaviorLoadSys) {
  EcsView* loadView = ecs_world_view_t(world, BehaviorLoadView);
  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    SceneBehaviorResourceComp* res = ecs_view_write_t(itr, SceneBehaviorResourceComp);

    if (!(res->flags & (SceneBehavior_ResourceAcquired | SceneBehavior_ResourceUnloading))) {
      asset_acquire(world, ecs_view_entity(itr));
      res->flags |= SceneBehavior_ResourceAcquired;
    }
  }
}

ecs_system_define(SceneBehaviorUnloadChangedSys) {
  EcsView* loadView = ecs_world_view_t(world, BehaviorLoadView);
  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    EcsEntityId                entity = ecs_view_entity(itr);
    SceneBehaviorResourceComp* res    = ecs_view_write_t(itr, SceneBehaviorResourceComp);

    const bool isLoaded   = ecs_world_has_t(world, entity, AssetLoadedComp);
    const bool isFailed   = ecs_world_has_t(world, entity, AssetFailedComp);
    const bool hasChanged = ecs_world_has_t(world, entity, AssetChangedComp);

    if (res->flags & SceneBehavior_ResourceAcquired && (isLoaded || isFailed) && hasChanged) {
      log_i("Unloading behavior asset", log_param("reason", fmt_text_lit("Asset changed")));

      asset_release(world, entity);
      res->flags &= ~SceneBehavior_ResourceAcquired;
      res->flags |= SceneBehavior_ResourceUnloading;
    }
    if (res->flags & SceneBehavior_ResourceUnloading && !isLoaded) {
      res->flags &= ~SceneBehavior_ResourceUnloading;
    }
  }
}

static void scene_brain_eval(
    const EcsEntityId entity, const SceneBrainComp* brain, const AssetBehaviorComp* behavior) {

  const AiResult res = ai_eval(&behavior->root, brain->blackboard);
  if (res != AiResult_Success) {
    log_w(
        "Brain behavior evaluated to 'failure'", log_param("entity", fmt_int(entity, .base = 16)));
  }
}

ecs_system_define(SceneBrainUpdateSys) {
  EcsView* brainEntities  = ecs_world_view_t(world, BrainEntityView);
  EcsView* behaviorAssets = ecs_world_view_t(world, BehaviorView);

  EcsIterator* behaviorItr = ecs_view_itr(behaviorAssets);

  u32 startedBehaviorLoads = 0;
  for (EcsIterator* itr = ecs_view_itr(brainEntities); ecs_view_walk(itr);) {
    const EcsEntityId     entity = ecs_view_entity(itr);
    const SceneBrainComp* brain  = ecs_view_write_t(itr, SceneBrainComp);

    // Evaluate the brain if the behavior asset is loaded.
    if (ecs_view_maybe_jump(behaviorItr, brain->behaviorAsset)) {
      const AssetBehaviorComp* behavior = ecs_view_read_t(behaviorItr, AssetBehaviorComp);
      scene_brain_eval(entity, brain, behavior);
      continue;
    }

    // Otherwise start loading the behavior.
    if (!ecs_world_has_t(world, brain->behaviorAsset, SceneBehaviorResourceComp)) {
      if (++startedBehaviorLoads < scene_brain_max_behavior_loads) {
        ecs_world_add_t(world, brain->behaviorAsset, SceneBehaviorResourceComp);
      }
    }
  }
}

ecs_module_init(scene_brain_module) {
  ecs_register_comp(SceneBrainComp, .destructor = ecs_destruct_brain_comp);
  ecs_register_comp(SceneBehaviorResourceComp, .combinator = ecs_combine_behavior_resource);

  ecs_register_view(BrainEntityView);
  ecs_register_view(BehaviorView);
  ecs_register_view(BehaviorLoadView);

  ecs_register_system(SceneBehaviorLoadSys, ecs_view_id(BehaviorLoadView));
  ecs_register_system(SceneBehaviorUnloadChangedSys, ecs_view_id(BehaviorLoadView));

  ecs_register_system(SceneBrainUpdateSys, ecs_view_id(BrainEntityView), ecs_view_id(BehaviorView));
}

const AiBlackboard* scene_brain_blackboard(const SceneBrainComp* brain) {
  return brain->blackboard;
}

AiBlackboard* scene_brain_blackboard_mutable(SceneBrainComp* brain) { return brain->blackboard; }

SceneBrainComp*
scene_brain_add(EcsWorld* world, const EcsEntityId entity, const EcsEntityId behaviorAsset) {
  diag_assert(ecs_world_exists(world, behaviorAsset));

  return ecs_world_add_t(
      world,
      entity,
      SceneBrainComp,
      .blackboard    = ai_blackboard_create(g_alloc_heap),
      .behaviorAsset = behaviorAsset);
}
