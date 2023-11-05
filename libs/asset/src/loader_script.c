#include "asset_script.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_thread.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "script_binder.h"
#include "script_diag.h"
#include "script_read.h"
#include "script_sig.h"

#include "repo_internal.h"

static ScriptBinder* g_scriptBinder;

static void script_bind(
    ScriptBinder*      binder,
    const String       name,
    const ScriptMask   retMask,
    const ScriptSigArg args[],
    const u8           argCount) {
  const ScriptSig* sig = script_sig_create(g_alloc_scratch, retMask, args, argCount);
  script_binder_declare(binder, name, sig, null);
}

static void script_binder_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_scriptBinder)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_scriptBinder) {
    ScriptBinder* binder = script_binder_create(g_alloc_persist);
    {
      const String     name = string_lit("self");
      const ScriptMask ret  = script_mask_entity;
      script_bind(binder, name, ret, null, 0);
    }
    {
      const String       name   = string_lit("exists");
      const ScriptMask   ret    = script_mask_bool;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("position");
      const ScriptMask   ret    = script_mask_vec3 | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("rotation");
      const ScriptMask   ret    = script_mask_quat | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("scale");
      const ScriptMask   ret    = script_mask_num | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("name");
      const ScriptMask   ret    = script_mask_str | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("faction");
      const ScriptMask   ret    = script_mask_str | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("health");
      const ScriptMask   ret    = script_mask_num | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("time");
      const ScriptMask   ret    = script_mask_num | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("type"), script_mask_str},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("nav_query");
      const ScriptMask   ret    = script_mask_vec3 | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("pos"), script_mask_vec3},
          {string_lit("type"), script_mask_str},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("nav_target");
      const ScriptMask   ret    = script_mask_vec3 | script_mask_entity | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("line_of_sight");
      const ScriptMask   ret    = script_mask_num | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("src"), script_mask_entity},
          {string_lit("dst"), script_mask_entity},
          {string_lit("radius"), script_mask_num},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("capable");
      const ScriptMask   ret    = script_mask_bool | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
          {string_lit("capability"), script_mask_str},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("active");
      const ScriptMask   ret    = script_mask_bool | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
          {string_lit("activity"), script_mask_str},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("target_primary");
      const ScriptMask   ret    = script_mask_entity | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("target_range_min");
      const ScriptMask   ret    = script_mask_num | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("target_range_max");
      const ScriptMask   ret    = script_mask_num | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("spawn");
      const ScriptMask   ret    = script_mask_entity | script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("prefabId"), script_mask_str},
          {string_lit("pos"), script_mask_vec3},
          {string_lit("rot"), script_mask_quat},
          {string_lit("scale"), script_mask_num},
          {string_lit("faction"), script_mask_str},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("destroy");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("destroy_after");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
          {string_lit("ownerOrDelay"), script_mask_entity | script_mask_time},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("teleport");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
          {string_lit("pos"), script_mask_vec3},
          {string_lit("rot"), script_mask_quat},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("nav_travel");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
          {string_lit("target"), script_mask_entity | script_mask_vec3},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("nav_stop");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("attach");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
          {string_lit("target"), script_mask_entity},
          {string_lit("jointName"), script_mask_str},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("detach");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("damage");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
          {string_lit("amount"), script_mask_num},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("attack");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_entity},
          {string_lit("target"), script_mask_entity | script_mask_null},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String       name   = string_lit("debug_log");
      const ScriptMask   ret    = script_mask_null;
      const ScriptSigArg args[] = {
          {string_lit("v"), script_mask_any},
      };
      script_bind(binder, name, ret, args, array_elems(args));
    }
    {
      const String     name = string_lit("debug_break");
      const ScriptMask ret  = script_mask_null;
      script_bind(binder, name, ret, null, 0);
    }

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
    log_e("Script load error", log_param("error", fmt_text(msg)));
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
