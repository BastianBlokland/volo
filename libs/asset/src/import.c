#include "asset_manager.h"
#include "asset_register.h"
#include "asset_script.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_path.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "format_internal.h"
#include "import_internal.h"

typedef enum {
  AssetImportType_Mesh,

  AssetImportType_Count,
  AssetImportType_Sentinel = sentinel_u32
} AssetImportType;

typedef struct {
  bool                 reloading;
  EcsEntityId          asset;
  const ScriptProgram* program;
} AssetImportScript;

typedef struct {
  u32      importHash;
  DynArray scripts; // AssetImportScript[]
} AssetImportHandler;

static const String g_assetImportScriptPaths[AssetImportType_Count] = {
    [AssetImportType_Mesh] = string_static("scripts/import/mesh/*.script"),
};

ecs_comp_define(AssetImportEnvComp) { AssetImportHandler handlers[AssetImportType_Count]; };

static void ecs_destruct_import_env_comp(void* data) {
  AssetImportEnvComp* comp = data;
  for (AssetImportType type = 0; type != AssetImportType_Count; ++type) {
    dynarray_destroy(&comp->handlers[type].scripts);
  }
}

static AssetImportType import_type_for_format(const AssetFormat format) {
  switch (format) {
  case AssetFormat_MeshGltf:
    return AssetImportType_Mesh;
  default:
    return AssetImportType_Sentinel;
  }
}

MAYBE_UNUSED static AssetImportType import_type_for_domain(const AssetScriptDomain domain) {
  switch (domain) {
  case AssetScriptDomain_ImportMesh:
    return AssetImportType_Mesh;
  default:
    return AssetImportType_Sentinel;
  }
}

static AssetImportEnvComp* import_env_init(EcsWorld* world, AssetManagerComp* manager) {
  AssetImportEnvComp* res = ecs_world_add_t(world, ecs_world_global(world), AssetImportEnvComp);

  EcsEntityId assets[asset_query_max_results];
  u32         assetCount;
  for (AssetImportType type = 0; type != AssetImportType_Count; ++type) {
    assetCount = asset_query(world, manager, g_assetImportScriptPaths[type], assets);

    AssetImportHandler* handler = &res->handlers[type];
    handler->scripts            = dynarray_create_t(g_allocHeap, AssetImportScript, assetCount);
    for (u32 i = 0; i != assetCount; ++i) {
      asset_acquire(world, assets[i]);
      *dynarray_push_t(&handler->scripts, AssetImportScript) = (AssetImportScript){
          .asset = assets[i],
      };
    }
  }
  return res;
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

static void asset_import_init_handler(
    EcsWorld*             world,
    const AssetImportType type,
    AssetImportHandler*   handler,
    EcsIterator*          scriptItr) {
  (void)type;
  /**
   * Update the import scripts.
   * NOTE: Its important to refresh the program pointers at the beginning of each frame as the ECS
   * can move component data around during flushes.
   * TODO: Don't reload import scripts while currently loading an asset.
   * TODO: Mark imported assets as changed when an importer script changes.
   */
  handler->importHash = 0;
  dynarray_for_t(&handler->scripts, AssetImportScript, script) {
    const bool isLoaded   = ecs_world_has_t(world, script->asset, AssetLoadedComp);
    const bool isFailed   = ecs_world_has_t(world, script->asset, AssetFailedComp);
    const bool hasChanged = ecs_world_has_t(world, script->asset, AssetChangedComp);

    if (hasChanged && !script->reloading && (isLoaded || isFailed)) {
      log_i("Reloading import script", log_param("reason", fmt_text_lit("Asset changed")));

      asset_release(world, script->asset);
      script->reloading = true;
    }

    if (!script->reloading && ecs_view_maybe_jump(scriptItr, script->asset)) {
      const AssetScriptComp* scriptComp = ecs_view_read_t(scriptItr, AssetScriptComp);
      diag_assert(type == import_type_for_domain(scriptComp->domain));

      handler->importHash = bits_hash_32_combine(handler->importHash, scriptComp->hash);
      script->program     = &scriptComp->prog;
    } else {
      script->program = null;
    }

    if (script->reloading && !isLoaded) {
      asset_acquire(world, script->asset);
      script->reloading = false;
    }
  }
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

  for (AssetImportType type = 0; type != AssetImportType_Count; ++type) {
    asset_import_init_handler(world, type, &importEnv->handlers[type], scriptItr);
  }
}

ecs_view_define(DeinitGlobalView) { ecs_access_write(AssetImportEnvComp); }

static void asset_import_deinit_handler(AssetImportHandler* handler) {
  // Clear program pointers; will be refreshed next frame.
  dynarray_for_t(&handler->scripts, AssetImportScript, script) { script->program = null; }
}

ecs_system_define(AssetImportDeinitSys) {
  EcsView*     globalView = ecs_world_view_t(world, DeinitGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (globalItr) {
    AssetImportEnvComp* importEnv = ecs_view_write_t(globalItr, AssetImportEnvComp);

    for (AssetImportType type = 0; type != AssetImportType_Count; ++type) {
      asset_import_deinit_handler(&importEnv->handlers[type]);
    }
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

bool asset_import_ready(const AssetImportEnvComp* env, const String assetId) {
  const AssetFormat     format = asset_format_from_ext(path_extension(assetId));
  const AssetImportType type   = import_type_for_format(format);
  if (type == AssetImportType_Sentinel) {
    return true; // No import-type defined for this format.
  }

  // Check if all import scripts are loaded.
  dynarray_for_t(&env->handlers[type].scripts, AssetImportScript, script) {
    if (!script->program) {
      return false;
    }
  }
  return true;
}

u32 asset_import_hash(const AssetImportEnvComp* env, const String assetId) {
  const AssetFormat     format = asset_format_from_ext(path_extension(assetId));
  const AssetImportType type   = import_type_for_format(format);
  if (type == AssetImportType_Sentinel) {
    return 0; // No import-type defined for this format.
  }
  return env->handlers[type].importHash;
}
