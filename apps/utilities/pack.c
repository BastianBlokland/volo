#include "app_ecs.h"
#include "asset_manager.h"
#include "asset_prefab.h"
#include "asset_product.h"
#include "asset_register.h"
#include "asset_weapon.h"
#include "cli_app.h"
#include "cli_help.h"
#include "cli_parse.h"
#include "cli_read.h"
#include "cli_validate.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_file.h"
#include "core_signal.h"
#include "data_read.h"
#include "data_utils.h"
#include "ecs_entity.h"
#include "ecs_utils.h"
#include "ecs_view.h"
#include "log_logger.h"

/**
 * Pack - Utility to pack assets.
 */

typedef struct {
  HeapArray_t(String) roots;
} PackConfig;

static DataMeta g_packConfigMeta;

static void pack_data_init(void) {
  // clang-format off
  data_reg_struct_t(g_dataReg, PackConfig);
  data_reg_field_t(g_dataReg, PackConfig, roots, data_prim_t(String), .container = DataContainer_HeapArray, .flags = DataFlags_NotEmpty);
  // clang-format on

  g_packConfigMeta = data_meta_t(t_PackConfig);
}

static bool pack_config_load(const String path, PackConfig* out) {
  bool       success = false;
  File*      file    = null;
  FileResult fileRes;
  if ((fileRes = file_create(g_allocHeap, path, FileMode_Open, FileAccess_Read, &file))) {
    log_e("Failed to open config file", log_param("err", fmt_text(file_result_str(fileRes))));
    goto Ret;
  }
  String data;
  if ((fileRes = file_map(file, 0 /* offset */, 0 /* size */, FileHints_Prefetch, &data))) {
    log_e("Failed to map config file", log_param("err", fmt_text(file_result_str(fileRes))));
    goto Ret;
  }
  DataReadResult result;
  const Mem      outMem = mem_create(out, sizeof(PackConfig));
  data_read_json(g_dataReg, data, g_allocHeap, g_packConfigMeta, outMem, &result);
  if (result.error) {
    log_e("Failed to parse config file", log_param("err", fmt_text(result.errorMsg)));
    goto Ret;
  }
  success = true;

Ret:
  if (file) {
    file_destroy(file);
  }
  return success;
}

typedef enum {
  PackState_Loading,
  PackState_Finished,
} PackState;

typedef struct {
  EcsEntityId entity;
  PackState   state;
  String      id; // NOTE: Available when load is finished.
} PackAsset;

ecs_comp_define(PackComp) {
  PackConfig cfg;
  DynArray   assets; // PackAsset[], sorted on entity.
  u64        frameIdx;
  u32        errorCount;
  bool       done;
};

static void ecs_destruct_texture_comp(void* data) {
  PackComp* comp = data;
  data_destroy(g_dataReg, g_allocHeap, g_packConfigMeta, mem_var(comp->cfg));
  dynarray_destroy(&comp->assets);
}

static i8 pack_compare_asset(const void* a, const void* b) {
  return ecs_compare_entity(field_ptr(a, PackAsset, entity), field_ptr(b, PackAsset, entity));
}

static void pack_push_asset(EcsWorld* world, PackComp* comp, const EcsEntityId entity) {
  const PackAsset target = {.entity = entity};
  PackAsset* entry = dynarray_find_or_insert_sorted(&comp->assets, pack_compare_asset, &target);
  if (entry->entity) {
    return; // Asset already added.
  }
  asset_acquire(world, entity);
  *entry = (PackAsset){.entity = entity, .state = PackState_Loading};
}

ecs_view_define(PackGlobalView) { ecs_access_write(PackComp); }

ecs_view_define(PackAssetView) {
  ecs_access_read(AssetComp);
  ecs_access_maybe_read(AssetPrefabMapComp);
  ecs_access_maybe_read(AssetProductMapComp);
  ecs_access_maybe_read(AssetWeaponMapComp);
}

static bool pack_asset_is_loaded(EcsWorld* world, const EcsEntityId asset) {
  return ecs_world_has_t(world, asset, AssetLoadedComp) ||
         ecs_world_has_t(world, asset, AssetFailedComp);
}

ecs_system_define(PackUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, PackGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (UNLIKELY(!globalItr)) {
    return; // Initialization failed; application will be terminated.
  }
  PackComp* pack = ecs_view_write_t(globalItr, PackComp);

  if (signal_is_received(Signal_Terminate) || signal_is_received(Signal_Interrupt)) {
    log_w("Packing interrupted", log_param("total-frames", fmt_int(pack->frameIdx)));
    pack->done = true;
    return;
  }

  EcsView*     assetView = ecs_world_view_t(world, PackAssetView);
  EcsIterator* assetItr  = ecs_view_itr(assetView);

  EcsEntityId refs[512];

  u32 busyAssets = 0;
  dynarray_for_t(&pack->assets, PackAsset, packAsset) {
    switch (packAsset->state) {
    case PackState_Loading: {
      ++busyAssets;
      if (!pack_asset_is_loaded(world, packAsset->entity)) {
        break; // Asset has not loaded yet; wait.
      }
      ecs_view_jump(assetItr, packAsset->entity);
      packAsset->state = PackState_Finished;
      packAsset->id    = asset_id(ecs_view_read_t(assetItr, AssetComp));

      asset_release(world, packAsset->entity); // Unload the asset.

      if (UNLIKELY(ecs_world_has_t(world, packAsset->entity, AssetFailedComp))) {
        ++pack->errorCount;
        break; // Asset failed to load.
      }
      u32                       refCount  = 0;
      const AssetPrefabMapComp* prefabMap = ecs_view_read_t(assetItr, AssetPrefabMapComp);
      if (prefabMap) {
        refCount += asset_prefab_refs(prefabMap, refs + refCount, array_elems(refs) - refCount);
      }
      const AssetProductMapComp* productMap = ecs_view_read_t(assetItr, AssetProductMapComp);
      if (productMap) {
        refCount += asset_product_refs(productMap, refs + refCount, array_elems(refs) - refCount);
      }
      const AssetWeaponMapComp* weaponMap = ecs_view_read_t(assetItr, AssetWeaponMapComp);
      if (weaponMap) {
        refCount += asset_weapon_refs(weaponMap, refs + refCount, array_elems(refs) - refCount);
      }
      for (u32 i = 0; i != refCount; ++i) {
        diag_assert(refs[i]);
        pack_push_asset(world, pack, refs[i]);
      }
      log_i(
          "Added asset",
          log_param("id", fmt_text(packAsset->id)),
          log_param("refs", fmt_int(refCount)));
    } break;
    case PackState_Finished:
      break;
    }
  }
  pack->done = !busyAssets;
  if (pack->done) {
    if (pack->errorCount) {
      log_e(
          "Packing failed",
          log_param("errors", fmt_int(pack->errorCount)),
          log_param("assets", fmt_int(pack->assets.size)),
          log_param("total-frames", fmt_int(pack->frameIdx)));
    } else {
      log_i(
          "Packing finished",
          log_param("assets", fmt_int(pack->assets.size)),
          log_param("total-frames", fmt_int(pack->frameIdx)));
    }
  }
}

ecs_module_init(pack_module) {
  ecs_register_comp(PackComp, .destructor = ecs_destruct_texture_comp);

  ecs_register_view(PackGlobalView);
  ecs_register_view(PackAssetView);

  ecs_register_system(PackUpdateSys, ecs_view_id(PackGlobalView), ecs_view_id(PackAssetView));
}

static CliId g_optConfigPath, g_optAssets, g_optHelp;

void app_ecs_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Volo asset packer"));

  g_optConfigPath = cli_register_arg(app, string_lit("config"), CliOptionFlags_Required);
  cli_register_desc(app, g_optConfigPath, string_lit("Path to a pack config file."));
  cli_register_validator(app, g_optConfigPath, cli_validate_file_regular);

  g_optAssets = cli_register_flag(app, 'a', string_lit("assets"), CliOptionFlags_Value);
  cli_register_desc(app, g_optAssets, string_lit("Path to asset directory."));
  cli_register_validator(app, g_optAssets, cli_validate_file_directory);

  g_optHelp = cli_register_flag(app, 'h', string_lit("help"), CliOptionFlags_None);
  cli_register_desc(app, g_optHelp, string_lit("Display this help page."));
  cli_register_exclusions(app, g_optHelp, g_optConfigPath);
  cli_register_exclusions(app, g_optHelp, g_optAssets);
}

bool app_ecs_validate(const CliApp* app, const CliInvocation* invoc) {
  if (cli_parse_provided(invoc, g_optHelp)) {
    cli_help_write_file(app, g_fileStdErr);
    return false;
  }
  return true;
}

void app_ecs_register(EcsDef* def, MAYBE_UNUSED const CliInvocation* invoc) {
  pack_data_init();

  asset_register(def);

  ecs_register_module(def, pack_module);
}

void app_ecs_init(EcsWorld* world, const CliInvocation* invoc) {
  const String assetPath = cli_read_string(invoc, g_optAssets, string_lit("assets"));
  if (file_stat_path_sync(assetPath).type != FileType_Directory) {
    log_e("Asset directory not found", log_param("path", fmt_path(assetPath)));
    return;
  }
  const String cfgPath = cli_read_string(invoc, g_optConfigPath, string_empty);
  PackConfig   cfg;
  if (!pack_config_load(cfgPath, &cfg)) {
    return;
  }

  PackComp* packComp = ecs_world_add_t(
      world,
      ecs_world_global(world),
      PackComp,
      .cfg    = cfg,
      .assets = dynarray_create_t(g_allocHeap, PackAsset, 512));

  const AssetManagerFlags assetFlg = AssetManagerFlags_DelayUnload;
  AssetManagerComp*       assetMan = asset_manager_create_fs(world, assetFlg, assetPath);

  EcsEntityId queryBuffer[asset_query_max_results];
  heap_array_for_t(cfg.roots, String, root) {
    const u32 count = asset_query(world, assetMan, *root, queryBuffer);
    for (u32 i = 0; i != count; ++i) {
      pack_push_asset(world, packComp, queryBuffer[i]);
    }
    if (UNLIKELY(!count)) {
      log_w("No assets found for root", log_param("root", fmt_text(*root)));
    }
  }
}

bool app_ecs_query_quit(EcsWorld* world) {
  PackComp* packComp = ecs_utils_write_first_t(world, PackGlobalView, PackComp);
  return !packComp || packComp->done;
}

i32 app_ecs_exit_code(EcsWorld* world) {
  PackComp* packComp = ecs_utils_write_first_t(world, PackGlobalView, PackComp);
  if (!packComp) {
    return 1;
  }
  return packComp->errorCount ? 2 : 0;
}

void app_ecs_set_frame(EcsWorld* world, const u64 frameIdx) {
  PackComp* packComp = ecs_utils_write_first_t(world, PackGlobalView, PackComp);
  if (LIKELY(packComp)) {
    packComp->frameIdx = frameIdx;
  }
}
