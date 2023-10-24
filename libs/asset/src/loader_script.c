#include "asset_script.h"
#include "core_alloc.h"
#include "core_thread.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "script_binder.h"
#include "script_diag.h"
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

    script_binder_declare(binder, string_lit("self"), null);
    script_binder_declare(binder, string_lit("exists"), null);
    script_binder_declare(binder, string_lit("position"), null);
    script_binder_declare(binder, string_lit("rotation"), null);
    script_binder_declare(binder, string_lit("scale"), null);
    script_binder_declare(binder, string_lit("name"), null);
    script_binder_declare(binder, string_lit("faction"), null);
    script_binder_declare(binder, string_lit("health"), null);
    script_binder_declare(binder, string_lit("time"), null);
    script_binder_declare(binder, string_lit("nav_query"), null);
    script_binder_declare(binder, string_lit("nav_target"), null);
    script_binder_declare(binder, string_lit("line_of_sight"), null);
    script_binder_declare(binder, string_lit("capable"), null);
    script_binder_declare(binder, string_lit("active"), null);
    script_binder_declare(binder, string_lit("target_primary"), null);
    script_binder_declare(binder, string_lit("target_range_min"), null);
    script_binder_declare(binder, string_lit("target_range_max"), null);
    script_binder_declare(binder, string_lit("spawn"), null);
    script_binder_declare(binder, string_lit("destroy"), null);
    script_binder_declare(binder, string_lit("destroy_after"), null);
    script_binder_declare(binder, string_lit("teleport"), null);
    script_binder_declare(binder, string_lit("nav_travel"), null);
    script_binder_declare(binder, string_lit("nav_stop"), null);
    script_binder_declare(binder, string_lit("attach"), null);
    script_binder_declare(binder, string_lit("detach"), null);
    script_binder_declare(binder, string_lit("damage"), null);
    script_binder_declare(binder, string_lit("attack"), null);
    script_binder_declare(binder, string_lit("debug_log"), null);

    script_binder_finalize(binder);
    g_scriptBinder = binder;
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define_public(AssetScriptComp);

static void ecs_destruct_script_comp(void* data) {
  AssetScriptComp* comp = data;
  string_maybe_free(g_alloc_heap, comp->sourceText);
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

  Allocator* tempAlloc = alloc_bump_create_stack(2 * usize_kibibyte);

  ScriptDoc*     doc      = script_create(g_alloc_heap);
  ScriptDiagBag* diags    = script_diag_bag_create(tempAlloc, ScriptDiagFilter_Error);
  ScriptSymBag*  symsNull = null;

  const ScriptExpr expr = script_read(doc, g_scriptBinder, src->data, diags, symsNull);

  const u32 diagCount = script_diag_count(diags, ScriptDiagFilter_All);
  for (u32 i = 0; i != diagCount; ++i) {
    const ScriptDiag* diag = script_diag_data(diags) + i;
    const String      msg  = script_diag_pretty_scratch(src->data, diag);
    log_e("Script error", log_param("error", fmt_text(msg)));
  }

  script_diag_bag_destroy(diags);

  if (UNLIKELY(sentinel_check(expr) || diagCount > 0)) {
    goto Error;
  }

  ecs_world_add_t(
      world,
      entity,
      AssetScriptComp,
      .sourceText = string_maybe_dup(g_alloc_heap, src->data),
      .doc        = doc,
      .expr       = expr);

  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  goto Cleanup;

Error:
  ecs_world_add_empty_t(world, entity, AssetFailedComp);
  script_destroy(doc);

Cleanup:
  asset_repo_source_close(src);
}
