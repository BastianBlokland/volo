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

/**
 * Demo application for render clearing.
 */

static int run_app() {

  log_i("App starting", log_param("pid", fmt_int(g_thread_pid)));

  EcsDef* def = def = ecs_def_create(g_alloc_heap);
  gap_register(def);
  rend_register(def);

  EcsWorld*  world  = ecs_world_create(g_alloc_heap, def);
  EcsRunner* runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_DumpGraphDot);

  log_i("App loop running");

  const EcsEntityId window =
      gap_window_create(world, GapWindowFlags_Default, gap_vector(1024, 768));
  rend_canvas_create(world, window, rend_soothing_purple);

  u64 tickCount = 0;
  while (ecs_world_exists(world, window)) {
    ecs_run_sync(runner);
    thread_sleep(time_second / 30);
    ++tickCount;
  }

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

  CliApp*        app   = cli_app_create(g_alloc_heap, string_lit("Volo Render Clear Demo"));
  CliInvocation* invoc = cli_parse(app, argc - 1, argv + 1);
  if (cli_parse_result(invoc) == CliParseResult_Fail) {
    cli_failure_write_file(invoc, g_file_stderr);
    exitCode = 2;
    goto exit;
  }

  exitCode = run_app();

exit:
  cli_parse_destroy(invoc);
  cli_app_destroy(app);

  log_teardown();
  jobs_teardown();
  core_teardown();
  return exitCode;
}
