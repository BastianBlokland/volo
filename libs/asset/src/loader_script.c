#include "asset_script.h"
#include "core_alloc.h"
#include "core_thread.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "script_binder.h"
#include "script_read.h"

#include "repo_internal.h"

static ScriptBinder* g_scriptBinder;

static void script_binder_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_scriptBinder)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_scriptBinder) {
    ScriptBinder* binder = script_binder_create(g_alloc_persist);

    script_binder_declare(binder, string_hash_lit("self"), null);
    script_binder_declare(binder, string_hash_lit("print"), null);
    script_binder_declare(binder, string_hash_lit("exists"), null);
    script_binder_declare(binder, string_hash_lit("position"), null);
    script_binder_declare(binder, string_hash_lit("rotation"), null);
    script_binder_declare(binder, string_hash_lit("scale"), null);
    script_binder_declare(binder, string_hash_lit("name"), null);
    script_binder_declare(binder, string_hash_lit("faction"), null);
    script_binder_declare(binder, string_hash_lit("time"), null);
    script_binder_declare(binder, string_hash_lit("nav_query"), null);
    script_binder_declare(binder, string_hash_lit("spawn"), null);
    script_binder_declare(binder, string_hash_lit("destroy"), null);
    script_binder_declare(binder, string_hash_lit("destroy_after"), null);
    script_binder_declare(binder, string_hash_lit("teleport"), null);
    script_binder_declare(binder, string_hash_lit("attach"), null);
    script_binder_declare(binder, string_hash_lit("detach"), null);

    script_binder_finalize(binder);
    g_scriptBinder = binder;
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define_public(AssetScriptComp);

static void ecs_destruct_script_comp(void* data) {
  AssetScriptComp* comp = data;
  script_destroy((ScriptDoc*)comp->doc);
}

ecs_view_define(ScriptUnloadView) {
  ecs_access_with(AssetScriptComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any script-asset component for unloaded assets.
 */
ecs_system_define(ScriptUnloadAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, ScriptUnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetScriptComp);
  }
}

ecs_module_init(asset_script_module) {
  script_binder_init();

  ecs_register_comp(AssetScriptComp, .destructor = ecs_destruct_script_comp);

  ecs_register_view(ScriptUnloadView);

  ecs_register_system(ScriptUnloadAssetSys, ecs_view_id(ScriptUnloadView));
}

void asset_load_script(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;

  ScriptDoc* doc = script_create(g_alloc_heap);

  ScriptReadResult readRes;
  script_read(doc, g_scriptBinder, src->data, &readRes);

  if (UNLIKELY(readRes.type != ScriptResult_Success)) {
    const String errScratch = script_read_result_scratch(doc, &readRes);
    log_e("Invalid script", log_param("error", fmt_text(errScratch)));
    goto Error;
  }

  ecs_world_add_t(world, entity, AssetScriptComp, .doc = doc, .expr = readRes.expr);

  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  goto Cleanup;

Error:
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
  script_destroy(doc);

Cleanup:
  asset_repo_source_close(src);
}
