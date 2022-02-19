#include "asset.h"
#include "cli.h"
#include "core.h"
#include "core_file.h"
#include "core_thread.h"
#include "ecs.h"
#include "gap.h"
#include "jobs.h"
#include "log.h"
#include "rend_register.h"
#include "scene_register.h"
#include "ui.h"

/**
 * Demo application that renders a user interface.
 */

static const GapVector g_windowSize = {1024, 768};

ecs_view_define(WindowView) {
  ecs_access_read(GapWindowComp);
  ecs_access_write(UiCanvasComp);
}

ecs_system_define(WindowUpdateSys) {
  EcsView* windowView = ecs_world_view_t(world, WindowView);
  for (EcsIterator* windowItr = ecs_view_itr(windowView); ecs_view_walk(windowItr);) {
    UiCanvasComp* canvas = ecs_view_write_t(windowItr, UiCanvasComp);
    ui_canvas_reset(canvas);
    ui_canvas_set_size(canvas, ui_vector(200, 200));
    ui_canvas_set_color(canvas, ui_color_red);
    ui_canvas_draw_glyph(canvas, 42);
  }
}

ecs_module_init(app_interface_module) {
  ecs_register_view(WindowView);

  ecs_register_system(WindowUpdateSys, ecs_view_id(WindowView));
}

static int app_run(const String assetPath) {
  log_i(
      "Application startup",
      log_param("asset-path", fmt_text(assetPath)),
      log_param("pid", fmt_int(g_thread_pid)));

  EcsDef* def = def = ecs_def_create(g_alloc_heap);
  ecs_register_module(def, app_interface_module);
  asset_register(def);
  gap_register(def);
  rend_register(def);
  scene_register(def);
  ui_register(def);

  EcsWorld*  world  = ecs_world_create(g_alloc_heap, def);
  EcsRunner* runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_DumpGraphDot);

  asset_manager_create_fs(
      world, AssetManagerFlags_TrackChanges | AssetManagerFlags_DelayUnload, assetPath);

  const EcsEntityId window = gap_window_create(world, GapWindowFlags_Default, g_windowSize);
  ui_canvas_create(world, window);

  do {
    ecs_run_sync(runner);
  } while (ecs_utils_any(world, WindowView));

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

  CliApp* app       = cli_app_create(g_alloc_heap, string_lit("Volo Interface Demo"));
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
