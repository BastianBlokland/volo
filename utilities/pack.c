#include "app_ecs.h"
#include "asset_graphic.h"
#include "asset_level.h"
#include "asset_manager.h"
#include "asset_pack.h"
#include "asset_prefab.h"
#include "asset_product.h"
#include "asset_register.h"
#include "asset_terrain.h"
#include "asset_weapon.h"
#include "cli_app.h"
#include "cli_parse.h"
#include "cli_read.h"
#include "cli_validate.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_file.h"
#include "core_path.h"
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
  PackState_Gathering,
  PackState_Waiting, // Wait a single frame to flush loads to the cache.
  PackState_Build,

  PackState_Interupted,
  PackState_Failed,
  PackState_Finished,
} PackState;

typedef struct {
  EcsEntityId entity;
  bool        loading;
  String      id; // NOTE: Available when load is finished.
} PackAsset;

ecs_comp_define(PackComp) {
  PackConfig cfg;
  String     outputPath;
  DynArray   assets; // PackAsset[], sorted on entity.
  TimeSteady timeStart;
  u64        frameIdx;
  u32        uncachedCount;
  PackState  state;
};

static void ecs_destruct_texture_comp(void* data) {
  PackComp* comp = data;
  data_destroy(g_dataReg, g_allocHeap, g_packConfigMeta, mem_var(comp->cfg));
  string_free(g_allocHeap, comp->outputPath);
  dynarray_destroy(&comp->assets);
}

static i8 pack_compare_asset(const void* a, const void* b) {
  return ecs_compare_entity(field_ptr(a, PackAsset, entity), field_ptr(b, PackAsset, entity));
}

ecs_view_define(PackGlobalView) {
  ecs_access_read(AssetImportEnvComp);
  ecs_access_write(AssetManagerComp);
  ecs_access_write(PackComp);
}

ecs_view_define(PackAssetView) {
  ecs_access_read(AssetComp);
  ecs_access_maybe_read(AssetGraphicComp);
  ecs_access_maybe_read(AssetLevelComp);
  ecs_access_maybe_read(AssetPrefabMapComp);
  ecs_access_maybe_read(AssetProductMapComp);
  ecs_access_maybe_read(AssetTerrainComp);
  ecs_access_maybe_read(AssetWeaponMapComp);
}

static bool pack_is_loaded(EcsWorld* world, const EcsEntityId asset) {
  return ecs_world_has_t(world, asset, AssetLoadedComp) ||
         ecs_world_has_t(world, asset, AssetFailedComp);
}

typedef enum {
  PackGatherResult_Busy,
  PackGatherResult_Failed,
  PackGatherResult_Finished,
} PackGatherResult;

static void pack_gather_asset(EcsWorld* world, PackComp* comp, const EcsEntityId entity) {
  const PackAsset target = {.entity = entity};
  PackAsset* entry = dynarray_find_or_insert_sorted(&comp->assets, pack_compare_asset, &target);
  if (entry->entity) {
    return; // Asset already added.
  }
  asset_acquire(world, entity);
  *entry = (PackAsset){.entity = entity, .loading = true};
}

static PackGatherResult pack_gather_update(
    EcsWorld* world, PackComp* pack, AssetManagerComp* assetMan, EcsIterator* assetItr) {
  EcsEntityId refs[512];
  bool        finished = true;
  bool        error    = false;

  dynarray_for_t(&pack->assets, PackAsset, packAsset) {
    if (!packAsset->loading) {
      continue; // Already processed.
    }
    finished = false;
    if (!pack_is_loaded(world, packAsset->entity)) {
      continue; // Asset has not loaded yet; wait.
    }
    ecs_view_jump(assetItr, packAsset->entity);
    const AssetComp* assetComp = ecs_view_read_t(assetItr, AssetComp);
    const bool       isCached  = asset_is_cached(assetComp);

    packAsset->loading = false;
    packAsset->id      = asset_id(assetComp);
    if (!isCached) {
      ++pack->uncachedCount;
    }

    asset_release(world, packAsset->entity); // Unload the asset.

    if (UNLIKELY(ecs_world_has_t(world, packAsset->entity, AssetFailedComp))) {
      error = true;
      continue; // Asset failed to load.
    }
    u32                     refCount    = 0;
    const AssetGraphicComp* graphicComp = ecs_view_read_t(assetItr, AssetGraphicComp);
    if (graphicComp) {
      refCount += asset_graphic_refs(graphicComp, refs + refCount, array_elems(refs) - refCount);
    }
    const AssetLevelComp* levelComp = ecs_view_read_t(assetItr, AssetLevelComp);
    if (levelComp) {
      refCount += asset_level_refs(
          levelComp, world, assetMan, refs + refCount, array_elems(refs) - refCount);
    }
    const AssetPrefabMapComp* prefabMap = ecs_view_read_t(assetItr, AssetPrefabMapComp);
    if (prefabMap) {
      refCount += asset_prefab_refs(prefabMap, refs + refCount, array_elems(refs) - refCount);
    }
    const AssetProductMapComp* productMap = ecs_view_read_t(assetItr, AssetProductMapComp);
    if (productMap) {
      refCount += asset_product_refs(productMap, refs + refCount, array_elems(refs) - refCount);
    }
    const AssetTerrainComp* terrainComp = ecs_view_read_t(assetItr, AssetTerrainComp);
    if (terrainComp) {
      refCount += asset_terrain_refs(terrainComp, refs + refCount, array_elems(refs) - refCount);
    }
    const AssetWeaponMapComp* weaponMap = ecs_view_read_t(assetItr, AssetWeaponMapComp);
    if (weaponMap) {
      refCount += asset_weapon_refs(weaponMap, refs + refCount, array_elems(refs) - refCount);
    }
    for (u32 i = 0; i != refCount; ++i) {
      diag_assert(refs[i]);
      pack_gather_asset(world, pack, refs[i]);
    }
    log_i(
        "Gathered asset",
        log_param("id", fmt_text(packAsset->id)),
        log_param("refs", fmt_int(refCount)),
        log_param("cached", fmt_bool(isCached)));
  }

  if (error) {
    log_e(
        "Packing failed",
        log_param("assets", fmt_int(pack->assets.size)),
        log_param("frames", fmt_int(pack->frameIdx)));
    return PackGatherResult_Failed;
  }
  if (finished) {
    log_i(
        "Gathering finished",
        log_param("assets", fmt_int(pack->assets.size)),
        log_param("assets-uncached", fmt_int(pack->uncachedCount)),
        log_param("frames", fmt_int(pack->frameIdx)));
    return PackGatherResult_Finished;
  }
  return PackGatherResult_Busy;
}

static String pack_write_path(const PackComp* pack) {
  return fmt_write_scratch("{}.tmp", fmt_text(pack->outputPath));
}

static bool pack_build(PackComp* p, AssetManagerComp* assetMan, const AssetImportEnvComp* impEnv) {
  File*                 file       = null;
  const FileAccessFlags fileAccess = FileAccess_Read | FileAccess_Write;
  FileResult            fileRes    = file_create_dir_sync(path_parent(p->outputPath));
  if (LIKELY(fileRes == FileResult_Success)) {
    fileRes = file_create(g_allocHeap, pack_write_path(p), FileMode_Create, fileAccess, &file);
  }
  if (UNLIKELY(fileRes != FileResult_Success)) {
    goto FileError;
  }
  AssetPacker* packer = asset_packer_create(g_allocHeap, (u32)p->assets.size);

  bool success = true;
  dynarray_for_t(&p->assets, PackAsset, packAsset) {
    diag_assert(!packAsset->loading && !string_is_empty(packAsset->id));

    if (!asset_packer_push(packer, assetMan, impEnv, packAsset->id)) {
      log_e("Failed to push file", log_param("path", fmt_text(packAsset->id)));
      success = false;
    }
  }
  if (success) {
    AssetPackerStats stats;
    if (asset_packer_write(packer, assetMan, impEnv, file, &stats)) {
      log_i(
          "Pack file build",
          log_param("path", fmt_path(p->outputPath)),
          log_param("size", fmt_size(stats.size)),
          log_param("padding", fmt_size(stats.padding)),
          log_param("header-size", fmt_size(stats.headerSize)),
          log_param("entries", fmt_int(stats.entries)),
          log_param("regions", fmt_int(stats.regions)),
          log_param("blocks", fmt_int(stats.blocks)));
    } else {
      log_e("Failed to build pack file");
      success = false;
    }
  }
  asset_packer_destroy(packer);

  file_destroy(file);
  if (UNLIKELY(fileRes = file_rename(pack_write_path(p), p->outputPath))) {
    file_delete_sync(pack_write_path(p));
    goto FileError;
  }

  const TimeDuration duration = time_steady_duration(p->timeStart, time_steady_clock());
  log_i(
      "Packing finished",
      log_param("success", fmt_bool(success)),
      log_param("duration", fmt_duration(duration)));
  return success;

FileError:
  log_e(
      "Failed to create output file",
      log_param("path", fmt_path(p->outputPath)),
      log_param("error", fmt_text(file_result_str(fileRes))));
  return false;
}

ecs_system_define(PackUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, PackGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (UNLIKELY(!globalItr)) {
    return; // Initialization failed; application will be terminated.
  }
  PackComp*                 pack      = ecs_view_write_t(globalItr, PackComp);
  AssetManagerComp*         assetMan  = ecs_view_write_t(globalItr, AssetManagerComp);
  const AssetImportEnvComp* importEnv = ecs_view_read_t(globalItr, AssetImportEnvComp);

  if (signal_is_received(Signal_Terminate) || signal_is_received(Signal_Interrupt)) {
    log_w("Packing interrupted", log_param("frames", fmt_int(pack->frameIdx)));
    pack->state = PackState_Interupted;
    return;
  }

  EcsView*     assetView = ecs_world_view_t(world, PackAssetView);
  EcsIterator* assetItr  = ecs_view_itr(assetView);

  switch (pack->state) {
  case PackState_Gathering: {
    const PackGatherResult gatherRes = pack_gather_update(world, pack, assetMan, assetItr);
    if (gatherRes == PackGatherResult_Failed) {
      pack->state = PackState_Failed;
    } else if (gatherRes == PackGatherResult_Finished) {
      ++pack->state;
    }
  } break;
  case PackState_Waiting:
    ++pack->state;
    break;
  case PackState_Build:
    if (pack_build(pack, assetMan, importEnv)) {
      pack->state = PackState_Finished;
    } else {
      pack->state = PackState_Failed;
    }
    break;
  case PackState_Interupted:
  case PackState_Failed:
  case PackState_Finished:
    break;
  }
}

ecs_module_init(pack_module) {
  ecs_register_comp(PackComp, .destructor = ecs_destruct_texture_comp);

  ecs_register_view(PackGlobalView);
  ecs_register_view(PackAssetView);

  ecs_register_system(PackUpdateSys, ecs_view_id(PackGlobalView), ecs_view_id(PackAssetView));
}

static CliId g_optConfigPath, g_optAssetsPath, g_optOutputPath;

void app_ecs_configure(CliApp* app) {
  cli_app_register_desc(app, string_lit("Volo asset packer"));

  g_optConfigPath = cli_register_arg(app, string_lit("config"), CliOptionFlags_Required);
  cli_register_desc(app, g_optConfigPath, string_lit("Path to a pack config file."));
  cli_register_validator(app, g_optConfigPath, cli_validate_file_regular);

  g_optAssetsPath = cli_register_flag(app, 'a', string_lit("assets"), CliOptionFlags_Value);
  cli_register_desc(app, g_optAssetsPath, string_lit("Path to asset directory."));
  cli_register_validator(app, g_optAssetsPath, cli_validate_file_directory);

  g_optOutputPath = cli_register_flag(app, 'o', string_lit("output"), CliOptionFlags_Value);
  cli_register_desc(app, g_optOutputPath, string_lit("Output file path."));
}

void app_ecs_register(EcsDef* def, MAYBE_UNUSED const CliInvocation* invoc) {
  pack_data_init();

  asset_register(def);

  ecs_register_module(def, pack_module);
}

void app_ecs_init(EcsWorld* world, const CliInvocation* invoc) {
  const String assetPath = cli_read_string(invoc, g_optAssetsPath, string_lit("assets"));
  if (file_stat_path_sync(assetPath).type != FileType_Directory) {
    log_e("Asset directory not found", log_param("path", fmt_path(assetPath)));
    return;
  }
  const String outputPath = cli_read_string(invoc, g_optOutputPath, string_lit("assets.blob"));
  if (string_is_empty(outputPath)) {
    log_e("Invalid output path", log_param("path", fmt_path(outputPath)));
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
      .cfg        = cfg,
      .outputPath = string_dup(g_allocHeap, path_build_scratch(outputPath)),
      .assets     = dynarray_create_t(g_allocHeap, PackAsset, 512),
      .timeStart  = time_steady_clock());

  const AssetManagerFlags assetFlags = AssetManagerFlags_PortableCache;
  AssetManagerComp*       assetMan   = asset_manager_create_fs(world, assetFlags, assetPath);

  EcsEntityId queryBuffer[asset_query_max_results];
  heap_array_for_t(cfg.roots, String, root) {
    const u32 count = asset_query(world, assetMan, *root, queryBuffer);
    for (u32 i = 0; i != count; ++i) {
      pack_gather_asset(world, packComp, queryBuffer[i]);
    }
    if (UNLIKELY(!count)) {
      log_w("No assets found for root", log_param("root", fmt_text(*root)));
    }
  }
}

bool app_ecs_query_quit(EcsWorld* world) {
  PackComp* packComp = ecs_utils_write_first_t(world, PackGlobalView, PackComp);
  return !packComp || packComp->state >= PackState_Interupted;
}

i32 app_ecs_exit_code(EcsWorld* world) {
  PackComp* packComp = ecs_utils_write_first_t(world, PackGlobalView, PackComp);
  if (!packComp) {
    return 1;
  }
  if (packComp->state == PackState_Interupted || packComp->state == PackState_Failed) {
    return 2;
  }
  return 0;
}

void app_ecs_set_frame(EcsWorld* world, const u64 frameIdx) {
  PackComp* packComp = ecs_utils_write_first_t(world, PackGlobalView, PackComp);
  if (LIKELY(packComp)) {
    packComp->frameIdx = frameIdx;
  }
}
