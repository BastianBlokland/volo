#include "asset_manager.h"
#include "asset_register.h"
#include "asset_script.h"
#include "core_alloc.h"
#include "core_format.h"
#include "core_path.h"
#include "ecs_world.h"
#include "log_logger.h"
#include "script_binder.h"
#include "script_sig.h"

#include "format_internal.h"
#include "import_internal.h"

static const String g_assetImportScriptsPath = string_static("scripts/import/*.script");

ScriptBinder* g_assetScriptImportBinder;

typedef enum {
  AssetImportScript_Reloading = 1 << 0,
} AssetImportScriptFlags;

typedef struct {
  AssetImportScriptFlags flags : 8;
  u32                    version; // Incremented on reload.
  EcsEntityId            asset;
  const ScriptProgram*   program;
} AssetImportScript;

ecs_comp_define(AssetImportEnvComp) {
  DynArray scripts; // AssetImportScript[]
};

static void ecs_destruct_import_env_comp(void* data) {
  AssetImportEnvComp* comp = data;
  dynarray_destroy(&comp->scripts);
}

static bool import_enabled_for_format(const AssetFormat format) {
  switch (format) {
  case AssetFormat_MeshGltf:
    return true;
  default:
    return false;
  }
}

typedef struct {
  u32 dummy;
} AssetImportContext;

static ScriptVal eval_dummy(AssetImportContext* ctx, ScriptBinderCall* call) {
  (void)ctx;
  (void)call;
  return script_null();
}

static AssetImportEnvComp* import_env_init(EcsWorld* world, AssetManagerComp* manager) {
  DynArray scripts = dynarray_create_t(g_allocHeap, AssetImportScript, 16);

  EcsEntityId assets[asset_query_max_results];
  const u32   assetCount = asset_query(world, manager, g_assetImportScriptsPath, assets);

  for (u32 i = 0; i != assetCount; ++i) {
    asset_acquire(world, assets[i]);
    *dynarray_push_t(&scripts, AssetImportScript) = (AssetImportScript){.asset = assets[i]};
  }
  return ecs_world_add_t(world, ecs_world_global(world), AssetImportEnvComp, .scripts = scripts);
}

ecs_view_define(InitGlobalView) {
  ecs_access_maybe_write(AssetImportEnvComp);
  ecs_access_write(AssetManagerComp);
}

ecs_view_define(InitScriptView) {
  ecs_access_with(AssetLoadedComp);
  ecs_access_without(AssetFailedComp);
  ecs_access_without(AssetChangedComp);
  ecs_access_read(AssetScriptComp);
}

ecs_system_define(AssetImportInitSys) {
  EcsView*     globalView = ecs_world_view_t(world, InitGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return; // Global dependencies not initialized.
  }
  AssetManagerComp*   manager   = ecs_view_write_t(globalItr, AssetManagerComp);
  AssetImportEnvComp* importEnv = ecs_view_write_t(globalItr, AssetImportEnvComp);
  if (UNLIKELY(!importEnv)) {
    importEnv = import_env_init(world, manager);
  }

  EcsView*     scriptView = ecs_world_view_t(world, InitScriptView);
  EcsIterator* scriptItr  = ecs_view_itr(scriptView);

  /**
   * Update the import scripts.
   * NOTE: Its important to refresh the program pointers at the beginning of each frame as the ECS
   * can move component data around during flushes.
   */
  dynarray_for_t(&importEnv->scripts, AssetImportScript, script) {
    const bool isLoaded   = ecs_world_has_t(world, script->asset, AssetLoadedComp);
    const bool isFailed   = ecs_world_has_t(world, script->asset, AssetFailedComp);
    const bool hasChanged = ecs_world_has_t(world, script->asset, AssetChangedComp);

    if (hasChanged && !(script->flags & AssetImportScript_Reloading) && (isLoaded || isFailed)) {
      log_i("Reloading import script", log_param("reason", fmt_text_lit("Asset changed")));

      asset_release(world, script->asset);
      ++script->version;
      script->flags |= AssetImportScript_Reloading;
    }
    if (script->flags & AssetImportScript_Reloading && !isLoaded) {
      asset_acquire(world, script->asset);
      script->flags &= ~AssetImportScript_Reloading;
    }

    if (ecs_view_maybe_jump(scriptItr, script->asset)) {
      script->program = &ecs_view_read_t(scriptItr, AssetScriptComp)->prog;
    } else {
      script->program = null;
    }
  }
}

ecs_view_define(DeinitGlobalView) { ecs_access_write(AssetImportEnvComp); }

ecs_system_define(AssetImportDeinitSys) {
  EcsView*     globalView = ecs_world_view_t(world, DeinitGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (globalItr) {
    AssetImportEnvComp* importEnv = ecs_view_write_t(globalItr, AssetImportEnvComp);

    // Clear program pointers; will be refreshed next frame.
    dynarray_for_t(&importEnv->scripts, AssetImportScript, script) { script->program = null; }
  }
}

ecs_module_init(asset_import_module) {
  ecs_register_comp(AssetImportEnvComp, .destructor = ecs_destruct_import_env_comp);

  ecs_register_view(InitGlobalView);
  ecs_register_view(InitScriptView);
  ecs_register_view(DeinitGlobalView);

  ecs_register_system(AssetImportInitSys, ecs_view_id(InitGlobalView), ecs_view_id(InitScriptView));
  ecs_order(AssetImportInitSys, AssetOrder_Init);

  ecs_register_system(AssetImportDeinitSys, ecs_view_id(DeinitGlobalView));
  ecs_order(AssetImportDeinitSys, AssetOrder_Deinit);
}

typedef ScriptVal (*ImportBinderFunc)(AssetImportContext*, ScriptBinderCall*);

static void bind_eval(
    ScriptBinder*          binder,
    const String           name,
    const String           doc,
    const ScriptMask       retMask,
    const ScriptSigArg     args[],
    const u8               argCount,
    const ImportBinderFunc func) {
  const ScriptSig* sig = script_sig_create(g_allocScratch, retMask, args, argCount);
  // NOTE: Func pointer cast is needed to type-erase the context type.
  script_binder_declare(binder, name, doc, sig, (ScriptBinderFunc)func);
}

void asset_data_init_import(void) {
  const ScriptBinderFlags flags = ScriptBinderFlags_DisallowMemoryAccess;
  ScriptBinder* binder          = script_binder_create(g_allocPersist, string_lit("import"), flags);
  script_binder_filter_set(binder, string_lit("import/*.script"));

  // clang-format off
  {
    const String       name   = string_lit("dummy");
    const String       doc    = fmt_write_scratch("Set a dummy import config");
    const ScriptMask   ret    = script_mask_null;
    const ScriptSigArg args[] = {
        {string_lit("val"), script_mask_null},
    };
    bind_eval(binder, name, doc, ret, args, array_elems(args), eval_dummy);
  }
  // clang-format on

  script_binder_finalize(binder);
  g_assetScriptImportBinder = binder;
}

bool asset_import_ready(const AssetImportEnvComp* env, const String assetId) {
  const AssetFormat format = asset_format_from_ext(path_extension(assetId));
  if (!import_enabled_for_format(format)) {
    return true;
  }

  // Check if all import scripts are loaded.
  dynarray_for_t(&env->scripts, AssetImportScript, script) {
    if (!script->program) {
      return false;
    }
  }
  return true;
}
