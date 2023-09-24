#include "ai_eval.h"
#include "ai_tracer_record.h"
#include "asset_manager.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_brain.h"
#include "scene_knowledge.h"
#include "scene_register.h"
#include "script_mem.h"

#define scene_brain_max_behavior_loads 8

typedef enum {
  SceneBehavior_ResourceAcquired  = 1 << 0,
  SceneBehavior_ResourceUnloading = 1 << 1,
} SceneBehaviorFlags;

ecs_comp_define(SceneBrainComp) {
  SceneBrainFlags flags;
  AiTracerRecord* tracer;
  EcsEntityId     behaviorAsset;
};

ecs_comp_define(SceneBehaviorResourceComp) { SceneBehaviorFlags flags; };

static void ecs_destruct_brain_comp(void* data) {
  SceneBrainComp* brain = data;
  if (brain->tracer) {
    ai_tracer_record_destroy(brain->tracer);
  }
}

static void ecs_combine_behavior_resource(void* dataA, void* dataB) {
  SceneBehaviorResourceComp* a = dataA;
  SceneBehaviorResourceComp* b = dataB;
  a->flags |= b->flags;
}

ecs_view_define(BrainEntityView) {
  ecs_access_write(SceneBrainComp);
  ecs_access_write(SceneKnowledgeComp);
}

ecs_view_define(BehaviorView) {
  ecs_access_read(AssetComp);
  ecs_access_read(AssetBehaviorComp);
}

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
    const EcsEntityId        entity,
    SceneBrainComp*          brain,
    SceneKnowledgeComp*      knowledge,
    const AssetBehaviorComp* behavior,
    const AssetComp*         behaviorAsset) {

  if (UNLIKELY(brain->flags & SceneBrainFlags_PauseEvaluation)) {
    return;
  }

  AiEvalContext ctx = {
      .memory    = scene_knowledge_memory_mut(knowledge),
      .nodeDefs  = behavior->nodes,
      .nodeNames = behavior->nodeNames,
      .scriptDoc = behavior->scriptDoc,
  };

  if (brain->flags & SceneBrainFlags_Trace) {
    if (!brain->tracer) {
      brain->tracer = ai_tracer_record_create(g_alloc_heap);
    }
    ai_tracer_record_reset(brain->tracer);
    ctx.tracer = ai_tracer_record_api(brain->tracer);
  }

  const AiResult res = ai_eval(&ctx, AssetAiNodeRoot);
  if (UNLIKELY(res == AiResult_Failure)) {
    log_w(
        "Brain behavior evaluated to 'failure'",
        log_param("entity", fmt_int(entity, .base = 16)),
        log_param("behavior", fmt_text(asset_id(behaviorAsset))));
  }
}

ecs_system_define(SceneBrainUpdateSys) {
  EcsView* brainView      = ecs_world_view_t(world, BrainEntityView);
  EcsView* behaviorAssets = ecs_world_view_t(world, BehaviorView);

  EcsIterator* behaviorItr = ecs_view_itr(behaviorAssets);

  u32 startedBehaviorLoads = 0;
  for (EcsIterator* itr = ecs_view_itr_step(brainView, parCount, parIndex); ecs_view_walk(itr);) {
    const EcsEntityId   entity    = ecs_view_entity(itr);
    SceneBrainComp*     brain     = ecs_view_write_t(itr, SceneBrainComp);
    SceneKnowledgeComp* knowledge = ecs_view_write_t(itr, SceneKnowledgeComp);

    // Evaluate the brain if the behavior asset is loaded.
    if (ecs_view_maybe_jump(behaviorItr, brain->behaviorAsset)) {
      const AssetBehaviorComp* behavior      = ecs_view_read_t(behaviorItr, AssetBehaviorComp);
      const AssetComp*         behaviorAsset = ecs_view_read_t(behaviorItr, AssetComp);
      scene_brain_eval(entity, brain, knowledge, behavior, behaviorAsset);
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

  ecs_order(SceneBrainUpdateSys, SceneOrder_BrainUpdate);
  ecs_parallel(SceneBrainUpdateSys, 4);
}

const AiTracerRecord* scene_brain_tracer(const SceneBrainComp* brain) { return brain->tracer; }

SceneBrainFlags scene_brain_flags(const SceneBrainComp* brain) { return brain->flags; }

void scene_brain_flags_set(SceneBrainComp* brain, const SceneBrainFlags flags) {
  brain->flags |= flags;
}

void scene_brain_flags_unset(SceneBrainComp* brain, const SceneBrainFlags flags) {
  brain->flags &= ~flags;
}

void scene_brain_flags_toggle(SceneBrainComp* brain, const SceneBrainFlags flags) {
  brain->flags ^= flags;
}

EcsEntityId scene_brain_behavior(const SceneBrainComp* brain) { return brain->behaviorAsset; }

SceneBrainComp*
scene_brain_add(EcsWorld* world, const EcsEntityId entity, const EcsEntityId behaviorAsset) {
  diag_assert(ecs_world_exists(world, behaviorAsset));

  return ecs_world_add_t(world, entity, SceneBrainComp, .behaviorAsset = behaviorAsset);
}
