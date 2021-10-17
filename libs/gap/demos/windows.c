#include "cli.h"
#include "core.h"
#include "core_file.h"
#include "core_signal.h"
#include "core_thread.h"
#include "core_time.h"
#include "ecs.h"
#include "gap.h"
#include "jobs.h"
#include "log.h"

#define app_frequency 30

ecs_view_define(UpdateWindowView) { ecs_access_write(GapWindowComp); }

ecs_module_init(app_module) { ecs_register_view(UpdateWindowView); }

static int run_app() {
  EcsDef* def = def = ecs_def_create(g_alloc_heap);
  gap_register(def);
  ecs_register_module(def, app_module);

  EcsWorld*  world  = ecs_world_create(g_alloc_heap, def);
  EcsRunner* runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_DumpGraphDot);

  log_i("App loop running", log_param("frequency", fmt_int(app_frequency)));

  const TimeSteady startTimestamp = time_steady_clock();
  TimeDuration     time           = 0;
  u64              tickCount      = 0;

  const EcsEntityId window = gap_window_open(world, GapWindowFlags_Default, gap_vector(1024, 768));
  ecs_world_flush(world);

  EcsIterator* windowItr = ecs_view_itr(ecs_world_view_t(world, UpdateWindowView));

  while (ecs_world_exists(world, window)) {
    GapWindowComp*  windowComp = ecs_view_write_t(ecs_view_jump(windowItr, window), GapWindowComp);
    const GapVector windowSize = gap_window_param(windowComp, GapParam_WindowSize);
    gap_window_title_set(
        windowComp,
        fmt_write_scratch(
            "tick: {} size: {} cursor: {}, space: {}, click: {}, scroll: {}",
            fmt_int(tickCount),
            gap_vector_fmt(windowSize),
            gap_vector_fmt(gap_window_param(windowComp, GapParam_CursorPos)),
            fmt_bool(gap_window_key_down(windowComp, GapKey_Space)),
            fmt_bool(gap_window_key_pressed(windowComp, GapKey_MouseLeft)),
            gap_vector_fmt(gap_window_param(windowComp, GapParam_ScrollDelta))));

    if (gap_window_key_pressed(windowComp, GapKey_Escape)) {
      gap_window_close(windowComp);
    }

    if (gap_window_key_pressed(windowComp, GapKey_F)) {
      if (gap_window_mode(windowComp) == GapWindowMode_Fullscreen) {
        gap_window_resize(windowComp, gap_vector(1024, 768), GapWindowMode_Windowed);
      } else {
        gap_window_resize(windowComp, gap_vector(0, 0), GapWindowMode_Fullscreen);
      }
    }

    const GapVector scrollDelta = gap_window_param(windowComp, GapParam_ScrollDelta);
    if (scrollDelta.y) {
      gap_window_resize(
          windowComp,
          gap_vector(windowSize.x + scrollDelta.y, windowSize.y + scrollDelta.y),
          GapWindowMode_Windowed);
    }

    ecs_run_sync(runner);

    thread_sleep(time_second / app_frequency);
    ++tickCount;
    time = time_steady_duration(startTimestamp, time_steady_clock());
  }

  log_i(
      "App loop stopped",
      log_param("ticks", fmt_int(tickCount)),
      log_param("time", fmt_duration(time)));

  ecs_runner_destroy(runner);
  ecs_world_destroy(world);
  ecs_def_destroy(def);
  return 0;
}

int main(const int argc, const char** argv) {
  core_init();
  jobs_init();
  log_init();

  log_add_sink(g_logger, log_sink_pretty_default(g_alloc_heap, LogMask_All));
  log_add_sink(g_logger, log_sink_json_default(g_alloc_heap, LogMask_All));

  int exitCode = 0;

  CliApp*        app   = cli_app_create(g_alloc_heap, string_lit("Volo Gap Windows Demo"));
  CliInvocation* invoc = cli_parse(app, argc - 1, argv + 1);
  if (cli_parse_result(invoc) == CliParseResult_Fail) {
    cli_failure_write_file(invoc, g_file_stderr);
    exitCode = 2;
    goto exit;
  }

  log_i(
      "App startup",
      log_param("pid", fmt_int(g_thread_pid)),
      log_param("cpus", fmt_int(g_thread_core_count)));

  exitCode = run_app();

  log_i("App shutdown", log_param("exit-code", fmt_int(exitCode)));

exit:
  cli_parse_destroy(invoc);
  cli_app_destroy(app);

  log_teardown();
  jobs_teardown();
  core_teardown();
  return exitCode;
}
