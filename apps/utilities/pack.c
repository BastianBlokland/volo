#include "app_ecs.h"
#include "asset_manager.h"
#include "asset_register.h"
#include "cli_app.h"
#include "cli_help.h"
#include "cli_parse.h"
#include "cli_read.h"
#include "cli_validate.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_file.h"
#include "core_signal.h"
#include "data_read.h"
#include "data_utils.h"
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

ecs_comp_define(PackComp) {
  PackConfig cfg;
  u64        frameIdx;
  bool       done;
};

static void ecs_destruct_texture_comp(void* data) {
  PackComp* comp = data;
  data_destroy(g_dataReg, g_allocHeap, g_packConfigMeta, mem_var(comp->cfg));
}

static void pack_push_asset(PackComp* comp, const EcsEntityId entity) {
  (void)comp;
  (void)entity;
}

ecs_view_define(GlobalView) {
  ecs_access_write(PackComp);
  ecs_access_write(AssetManagerComp);
}

ecs_system_define(PackUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (UNLIKELY(!globalItr)) {
    return; // Initialization failed; application will be terminated.
  }
  PackComp*         pack   = ecs_view_write_t(globalItr, PackComp);
  AssetManagerComp* assets = ecs_view_write_t(globalItr, AssetManagerComp);

  (void)assets;

  if (signal_is_received(Signal_Terminate) || signal_is_received(Signal_Interrupt)) {
    log_w("Packing interrupted", log_param("total-frames", fmt_int(pack->frameIdx)));
    pack->done = true;
  }
}

ecs_module_init(pack_module) {
  ecs_register_comp(PackComp, .destructor = ecs_destruct_texture_comp);

  ecs_register_view(GlobalView);

  ecs_register_system(PackUpdateSys, ecs_view_id(GlobalView));
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

  PackComp* packComp = ecs_world_add_t(world, ecs_world_global(world), PackComp, .cfg = cfg);

  const AssetManagerFlags assetFlg = AssetManagerFlags_DelayUnload;
  AssetManagerComp*       assets   = asset_manager_create_fs(world, assetFlg, assetPath);

  EcsEntityId queryBuffer[asset_query_max_results];
  heap_array_for_t(cfg.roots, String, root) {
    const u32 count = asset_query(world, assets, *root, queryBuffer);
    for (u32 i = 0; i != count; ++i) {
      pack_push_asset(packComp, queryBuffer[i]);
    }
    if (UNLIKELY(!count)) {
      log_w("No assets found for root", log_param("root", fmt_text(*root)));
    }
  }
}

void app_ecs_set_frame(EcsWorld* world, const u64 frameIdx) {
  PackComp* packComp = ecs_utils_write_first_t(world, GlobalView, PackComp);
  if (LIKELY(packComp)) {
    packComp->frameIdx = frameIdx;
  }
}

bool app_ecs_query_quit(EcsWorld* world) {
  PackComp* packComp = ecs_utils_write_first_t(world, GlobalView, PackComp);
  return !packComp || packComp->done;
}
