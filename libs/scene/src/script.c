#include "asset_manager.h"
#include "asset_script.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_thread.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "scene_knowledge.h"
#include "scene_register.h"
#include "scene_script.h"
#include "script_binder.h"
#include "script_eval.h"
#include "script_mem.h"

#define scene_script_max_asset_loads 8

typedef struct {
  EcsEntityId entity;
  String      scriptId;
} SceneScriptBindCtx;

static ScriptVal scene_script_bind_print(void* ctxR, const ScriptVal* args, const usize argCount) {
  SceneScriptBindCtx* ctx = ctxR;

  if (!argCount) {
    return script_null();
  }

  Mem       bufferMem = alloc_alloc(g_alloc_scratch, usize_kibibyte, 1);
  DynString buffer    = dynstring_create_over(bufferMem);

  for (usize i = 0; i != argCount; ++i) {
    if (i) {
      dynstring_append_char(&buffer, ' ');
    }
    script_val_str_write(args[i], &buffer);
  }
  dynstring_append_char(&buffer, '\n');

  log_i(
      "script: {}",
      log_param("message", fmt_text(dynstring_view(&buffer))),
      log_param("entity", fmt_int(ctx->entity, .base = 16)),
      log_param("script", fmt_text(ctx->scriptId)));

  return args[argCount - 1];
}

static ScriptBinder* g_scriptBinder;

static void script_binder_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_scriptBinder)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_scriptBinder) {
    ScriptBinder* binder = script_binder_create(g_alloc_persist);

    script_binder_declare(binder, string_hash_lit("print"), scene_script_bind_print);

    script_binder_finalize(binder);
    g_scriptBinder = binder;
  }
  thread_spinlock_unlock(&g_initLock);
}

typedef enum {
  SceneScriptRes_ResourceAcquired  = 1 << 0,
  SceneScriptRes_ResourceUnloading = 1 << 1,
} SceneScriptResFlags;

ecs_comp_define(SceneScriptComp) {
  SceneScriptFlags flags;
  EcsEntityId      scriptAsset;
};

ecs_comp_define(SceneScriptResourceComp) { SceneScriptResFlags flags; };

static void ecs_combine_script_resource(void* dataA, void* dataB) {
  SceneScriptResourceComp* a = dataA;
  SceneScriptResourceComp* b = dataB;
  a->flags |= b->flags;
}

ecs_view_define(ScriptEntityView) {
  ecs_access_read(SceneScriptComp);
  ecs_access_write(SceneKnowledgeComp);
}

ecs_view_define(ResourceAssetView) {
  ecs_access_read(AssetComp);
  ecs_access_read(AssetScriptComp);
}

ecs_view_define(ResourceLoadView) { ecs_access_write(SceneScriptResourceComp); }

ecs_system_define(SceneScriptResourceLoadSys) {
  EcsView* loadView = ecs_world_view_t(world, ResourceLoadView);
  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    SceneScriptResourceComp* res = ecs_view_write_t(itr, SceneScriptResourceComp);

    if (!(res->flags & (SceneScriptRes_ResourceAcquired | SceneScriptRes_ResourceUnloading))) {
      asset_acquire(world, ecs_view_entity(itr));
      res->flags |= SceneScriptRes_ResourceAcquired;
    }
  }
}

ecs_system_define(SceneScriptResourceUnloadChangedSys) {
  EcsView* loadView = ecs_world_view_t(world, ResourceLoadView);
  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    EcsEntityId              entity = ecs_view_entity(itr);
    SceneScriptResourceComp* res    = ecs_view_write_t(itr, SceneScriptResourceComp);

    const bool isLoaded   = ecs_world_has_t(world, entity, AssetLoadedComp);
    const bool isFailed   = ecs_world_has_t(world, entity, AssetFailedComp);
    const bool hasChanged = ecs_world_has_t(world, entity, AssetChangedComp);

    if (res->flags & SceneScriptRes_ResourceAcquired && (isLoaded || isFailed) && hasChanged) {
      log_i("Unloading script asset", log_param("reason", fmt_text_lit("Asset changed")));

      asset_release(world, entity);
      res->flags &= ~SceneScriptRes_ResourceAcquired;
      res->flags |= SceneScriptRes_ResourceUnloading;
    }
    if (res->flags & SceneScriptRes_ResourceUnloading && !isLoaded) {
      res->flags &= ~SceneScriptRes_ResourceUnloading;
    }
  }
}

static void scene_script_eval(
    const EcsEntityId      entity,
    const SceneScriptComp* scriptInstance,
    SceneKnowledgeComp*    knowledge,
    const AssetScriptComp* scriptAsset,
    const AssetComp*       scriptAssetComp) {

  if (UNLIKELY(scriptInstance->flags & SceneScriptFlags_PauseEvaluation)) {
    return;
  }

  const ScriptDoc* doc  = scriptAsset->doc;
  const ScriptExpr expr = scriptAsset->expr;
  ScriptMem*       mem  = scene_knowledge_memory_mut(knowledge);

  SceneScriptBindCtx ctx = {
      .entity   = entity,
      .scriptId = asset_id(scriptAssetComp),
  };

  const ScriptEvalResult evalRes = script_eval(doc, mem, expr, g_scriptBinder, &ctx);

  if (UNLIKELY(evalRes.type != ScriptResult_Success)) {
    const String err = script_result_str(evalRes.type);
    log_w(
        "Script execution failed",
        log_param("error", fmt_text(err)),
        log_param("entity", fmt_int(entity, .base = 16)),
        log_param("script", fmt_text(asset_id(scriptAssetComp))));
  }
}

ecs_system_define(SceneScriptUpdateSys) {
  EcsView* scriptView        = ecs_world_view_t(world, ScriptEntityView);
  EcsView* resourceAssetView = ecs_world_view_t(world, ResourceAssetView);

  EcsIterator* resourceAssetItr = ecs_view_itr(resourceAssetView);

  u32 startedAssetLoads = 0;
  for (EcsIterator* itr = ecs_view_itr_step(scriptView, parCount, parIndex); ecs_view_walk(itr);) {
    const EcsEntityId   entity         = ecs_view_entity(itr);
    SceneScriptComp*    scriptInstance = ecs_view_write_t(itr, SceneScriptComp);
    SceneKnowledgeComp* knowledge      = ecs_view_write_t(itr, SceneKnowledgeComp);

    // Evaluate the script if the asset is loaded.
    if (ecs_view_maybe_jump(resourceAssetItr, scriptInstance->scriptAsset)) {
      const AssetScriptComp* scriptAsset     = ecs_view_read_t(resourceAssetItr, AssetScriptComp);
      const AssetComp*       scriptAssetComp = ecs_view_read_t(resourceAssetItr, AssetComp);
      scene_script_eval(entity, scriptInstance, knowledge, scriptAsset, scriptAssetComp);
      continue;
    }

    // Otherwise start loading the asset.
    if (!ecs_world_has_t(world, scriptInstance->scriptAsset, SceneScriptResourceComp)) {
      if (++startedAssetLoads < scene_script_max_asset_loads) {
        ecs_world_add_t(world, scriptInstance->scriptAsset, SceneScriptResourceComp);
      }
    }
  }
}

ecs_module_init(scene_script_module) {
  script_binder_init();

  ecs_register_comp(SceneScriptComp);
  ecs_register_comp(SceneScriptResourceComp, .combinator = ecs_combine_script_resource);

  ecs_register_view(ScriptEntityView);
  ecs_register_view(ResourceAssetView);
  ecs_register_view(ResourceLoadView);

  ecs_register_system(SceneScriptResourceLoadSys, ecs_view_id(ResourceLoadView));
  ecs_register_system(SceneScriptResourceUnloadChangedSys, ecs_view_id(ResourceLoadView));

  ecs_register_system(
      SceneScriptUpdateSys, ecs_view_id(ScriptEntityView), ecs_view_id(ResourceAssetView));

  ecs_order(SceneScriptUpdateSys, SceneOrder_ScriptUpdate);
  ecs_parallel(SceneScriptUpdateSys, 4);
}

SceneScriptFlags scene_script_flags(const SceneScriptComp* script) { return script->flags; }

void scene_script_flags_set(SceneScriptComp* script, const SceneScriptFlags flags) {
  script->flags |= flags;
}

void scene_script_flags_unset(SceneScriptComp* script, const SceneScriptFlags flags) {
  script->flags &= ~flags;
}

void scene_script_flags_toggle(SceneScriptComp* script, const SceneScriptFlags flags) {
  script->flags ^= flags;
}

EcsEntityId scene_script_asset(const SceneScriptComp* script) { return script->scriptAsset; }

SceneScriptComp*
scene_script_add(EcsWorld* world, const EcsEntityId entity, const EcsEntityId scriptAsset) {
  diag_assert(ecs_world_exists(world, scriptAsset));

  return ecs_world_add_t(world, entity, SceneScriptComp, .scriptAsset = scriptAsset);
}
