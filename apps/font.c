#include "asset.h"
#include "cli.h"
#include "core.h"
#include "core_file.h"
#include "core_math.h"
#include "ecs.h"
#include "gap.h"
#include "jobs.h"
#include "log.h"
#include "rend.h"
#include "scene_register.h"
#include "scene_text.h"

typedef enum {
  AppFlags_Init  = 1 << 0,
  AppFlags_Dirty = 1 << 1,
} AppFlags;

ecs_comp_define(AppComp) {
  AppFlags    flags;
  EcsEntityId window;
  EcsEntityId text;
};

ecs_view_define(GlobalView) { ecs_access_write(AppComp); }

ecs_view_define(WindowView) { ecs_access_read(GapWindowComp); }

ecs_system_define(AppUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  AppComp* app = ecs_view_write_t(globalItr, AppComp);

  if (app->flags & AppFlags_Init) {
    app->text = ecs_world_entity_create(world);
    ecs_world_add_t(
        world, app->text, SceneTextComp, .x = 100, .y = 100, .text = string_lit("Hello World!"));

    app->flags &= ~AppFlags_Init;
  }
}

ecs_module_init(app_font_module) {
  ecs_register_comp(AppComp);

  ecs_register_view(GlobalView);
  ecs_register_view(WindowView);

  ecs_register_system(AppUpdateSys, ecs_view_id(GlobalView), ecs_view_id(WindowView));
}

static int app_run(const String assetPath) {
  log_i("Application startup", log_param("asset-path", fmt_text(assetPath)));

  EcsDef* def = def = ecs_def_create(g_alloc_heap);
  ecs_register_module(def, app_font_module);
  asset_register(def);
  gap_register(def);
  rend_register(def);
  scene_register(def);

  EcsWorld*  world  = ecs_world_create(g_alloc_heap, def);
  EcsRunner* runner = ecs_runner_create(g_alloc_heap, world, 0);

  asset_manager_create_fs(world, AssetManagerFlags_TrackChanges, assetPath);

  const EcsEntityId win = gap_window_create(world, GapWindowFlags_Default, (GapVector){1024, 768});
  ecs_world_add_t(world, ecs_world_global(world), AppComp, .flags = AppFlags_Init, .window = win);

  while (ecs_world_exists(world, win)) {
    ecs_run_sync(runner);
  }

  ecs_runner_destroy(runner);
  ecs_world_destroy(world);
  ecs_def_destroy(def);

  log_i("Application shutdown");
  return 0;
}

int main(const int argc, const char** argv) {
  core_init();
  jobs_init();
  log_init();

  log_add_sink(g_logger, log_sink_pretty_default(g_alloc_heap, LogMask_All));
  log_add_sink(g_logger, log_sink_json_default(g_alloc_heap, LogMask_All));

  int exitCode = 0;

  CliApp* app       = cli_app_create(g_alloc_heap, string_lit("Volo Font Demo"));
  CliId   assetFlag = cli_register_flag(app, 'a', string_lit("assets"), CliOptionFlags_Required);
  cli_register_desc(app, assetFlag, string_lit("Path to asset directory."));

  CliInvocation* invoc = cli_parse(app, argc - 1, argv + 1);
  if (cli_parse_result(invoc) == CliParseResult_Fail) {
    cli_failure_write_file(invoc, g_file_stderr);
    exitCode = 2;
    goto exit;
  }

  const String assetPath = cli_read_string(invoc, assetFlag, string_empty);
  exitCode               = app_run(assetPath);

exit:
  cli_parse_destroy(invoc);
  cli_app_destroy(app);

  log_teardown();
  jobs_teardown();
  core_teardown();
  return exitCode;
}
