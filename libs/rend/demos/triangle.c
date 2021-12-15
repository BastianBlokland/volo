#include "asset.h"
#include "cli.h"
#include "core.h"
#include "core_file.h"
#include "core_thread.h"
#include "core_time.h"
#include "ecs.h"
#include "gap.h"
#include "jobs.h"
#include "log.h"
#include "rend.h"
#include "scene_graphic.h"
#include "scene_register.h"

/**
 * Demo application that renders a single triangle.
 */

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }

static void demo_add_triangle(EcsWorld* world) {
  AssetManagerComp* manager        = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
  const EcsEntityId triangleEntity = ecs_world_entity_create(world);
  ecs_world_add_t(
      world,
      triangleEntity,
      SceneGraphicComp,
      .asset = asset_lookup(world, manager, string_lit("graphics/triangle.gra")));
}

ecs_module_init(demo_triangle_module) { ecs_register_view(ManagerView); }

static int run_app(const String assetPath) {

  log_i("App starting", log_param("pid", fmt_int(g_thread_pid)));

  EcsDef* def = def = ecs_def_create(g_alloc_heap);
  asset_register(def);
  scene_register(def);
  gap_register(def);
  rend_register(def);
  ecs_register_module(def, demo_triangle_module);

  EcsWorld*  world  = ecs_world_create(g_alloc_heap, def);
  EcsRunner* runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_DumpGraphDot);

  log_i("App loop running");

  asset_manager_create_fs(world, assetPath);
  ecs_run_sync(runner);
  demo_add_triangle(world);

  const EcsEntityId window =
      gap_window_create(world, GapWindowFlags_Default, gap_vector(1024, 768));
  rend_canvas_create(world, window, rend_soothing_purple);

  u64 tickCount = 0;
  while (ecs_world_exists(world, window)) {
    ecs_run_sync(runner);
    thread_sleep(time_second / 30);
    ++tickCount;
  }

  rend_teardown(world);

  log_i(
      "App loop stopped",
      log_param("ticks", fmt_int(tickCount)),
      log_param("mem", fmt_size(alloc_stats_total())));

  ecs_runner_destroy(runner);
  ecs_world_destroy(world);
  ecs_def_destroy(def);

  log_i("App shutdown");

  return 0;
}

int main(const int argc, const char** argv) {
  core_init();
  jobs_init();
  log_init();

  log_add_sink(g_logger, log_sink_pretty_default(g_alloc_heap, LogMask_All));
  log_add_sink(g_logger, log_sink_json_default(g_alloc_heap, LogMask_All));

  int exitCode = 0;

  CliApp*     app = cli_app_create(g_alloc_heap, string_lit("Volo Render Triangle Demo"));
  const CliId assetFlag =
      cli_register_flag(app, 'a', string_lit("assets"), CliOptionFlags_Required);
  cli_register_desc(app, assetFlag, string_lit("Path to asset directory."));

  CliInvocation* invoc = cli_parse(app, argc - 1, argv + 1);
  if (cli_parse_result(invoc) == CliParseResult_Fail) {
    cli_failure_write_file(invoc, g_file_stderr);
    exitCode = 2;
    goto exit;
  }

  const String assetPath = cli_read_string(invoc, assetFlag, string_empty);
  exitCode               = run_app(assetPath);

exit:
  cli_parse_destroy(invoc);
  cli_app_destroy(app);

  log_teardown();
  jobs_teardown();
  core_teardown();
  return exitCode;
}
