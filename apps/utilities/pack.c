#include "app_ecs.h"
#include "asset_manager.h"
#include "asset_register.h"
#include "cli_app.h"
#include "cli_help.h"
#include "cli_parse.h"
#include "cli_read.h"
#include "cli_validate.h"
#include "core_file.h"
#include "core_signal.h"
#include "ecs_utils.h"
#include "ecs_view.h"
#include "log_logger.h"

ecs_comp_define(PackComp) {
  u64  frameIdx;
  bool done;
};

ecs_view_define(GlobalView) {
  ecs_access_write(PackComp);
  ecs_access_write(AssetManagerComp);
}

ecs_system_define(PackUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_at(globalView, ecs_world_global(world));

  PackComp*         pack   = ecs_view_write_t(globalItr, PackComp);
  AssetManagerComp* assets = ecs_view_write_t(globalItr, AssetManagerComp);

  (void)assets;

  if (signal_is_received(Signal_Terminate) || signal_is_received(Signal_Interrupt)) {
    log_w("Packing interrupted", log_param("total-frames", fmt_int(pack->frameIdx)));
    pack->done = true;
  }
}

ecs_module_init(pack_module) {
  ecs_register_comp(PackComp);

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
  asset_register(def);

  ecs_register_module(def, pack_module);
}

void app_ecs_init(EcsWorld* world, const CliInvocation* invoc) {
  const String assetPath = cli_read_string(invoc, g_optAssets, string_lit("assets"));
  if (file_stat_path_sync(assetPath).type != FileType_Directory) {
    log_e("Asset directory not found", log_param("path", fmt_path(assetPath)));
    return;
  }

  PackComp* packComp = ecs_world_add_t(world, ecs_world_global(world), PackComp);
  (void)packComp;

  const AssetManagerFlags assetFlg = AssetManagerFlags_DelayUnload;
  AssetManagerComp*       assets   = asset_manager_create_fs(world, assetFlg, assetPath);

  (void)assets;
  // TODO: Lookup asset roots.
}

void app_ecs_set_frame(EcsWorld* world, const u64 frameIdx) {
  ecs_utils_write_first_t(world, GlobalView, PackComp)->frameIdx = frameIdx;
}

bool app_ecs_query_quit(EcsWorld* world) {
  return ecs_utils_write_first_t(world, GlobalView, PackComp)->done;
}
