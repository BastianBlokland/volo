#include "cli.h"
#include "core.h"
#include "core_file.h"
#include "core_thread.h"
#include "core_time.h"
#include "ecs.h"
#include "gap.h"
#include "jobs.h"
#include "log.h"

/**
 * Demo application for testing window management.
 *
 * Controls:
 * - 'Escape / 'Q':     Close the focussed window.
 * - 'F':               Toggle fullscreen.
 * - 'H':               Toggle cursor hide.
 * - 'L':               Toggle cursor lock.
 * - 'Return':          Create a new window.
 * - Scrolling:         Resize the focussed window.
 * - Process interupt:  Close all windows.
 */

static const GapVector g_windowSize = {1024, 768};

static void window_update(EcsWorld* world, GapWindowComp* window, const u64 tickCount) {
  const GapVector windowSize = gap_window_param(window, GapParam_WindowSize);

  // Update the title.
  gap_window_title_set(
      window,
      fmt_write_scratch(
          "tick: {} size: {} cursor-pos: {}, cursor-delta: {}, space: {}, click: {}, scroll: {}",
          fmt_int(tickCount),
          gap_vector_fmt(windowSize),
          gap_vector_fmt(gap_window_param(window, GapParam_CursorPos)),
          gap_vector_fmt(gap_window_param(window, GapParam_CursorDelta)),
          fmt_bool(gap_window_key_down(window, GapKey_Space)),
          fmt_bool(gap_window_key_pressed(window, GapKey_MouseLeft)),
          gap_vector_fmt(gap_window_param(window, GapParam_ScrollDelta))));

  // Close with 'Q'.
  if (gap_window_key_pressed(window, GapKey_Q)) {
    gap_window_close(window);
  }

  // Toggle fullscreen with 'F'.
  if (gap_window_key_pressed(window, GapKey_F)) {
    if (gap_window_mode(window) == GapWindowMode_Fullscreen) {
      gap_window_resize(window, g_windowSize, GapWindowMode_Windowed);
    } else {
      gap_window_resize(window, gap_vector(0, 0), GapWindowMode_Fullscreen);
    }
  }

  // Toggle cursor hide with 'H'.
  if (gap_window_key_pressed(window, GapKey_H)) {
    if (gap_window_flags(window) & GapWindowFlags_CursorHide) {
      gap_window_flags_unset(window, GapWindowFlags_CursorHide);
    } else {
      gap_window_flags_set(window, GapWindowFlags_CursorHide);
    }
  }

  // Toggle cursor lock with 'L'.
  if (gap_window_key_pressed(window, GapKey_L)) {
    if (gap_window_flags(window) & GapWindowFlags_CursorLock) {
      gap_window_flags_unset(window, GapWindowFlags_CursorLock);
    } else {
      gap_window_flags_set(window, GapWindowFlags_CursorLock);
    }
  }

  // Create a new window with 'Return'.
  if (gap_window_key_pressed(window, GapKey_Return)) {
    gap_window_create(world, GapWindowFlags_Default, g_windowSize);
  }

  // Resize the window by scrolling.
  const GapVector scrollDelta = gap_window_param(window, GapParam_ScrollDelta);
  if (gap_window_mode(window) == GapWindowMode_Windowed && (scrollDelta.x || scrollDelta.y)) {
    gap_window_resize(
        window,
        gap_vector(windowSize.x + scrollDelta.x, windowSize.y + scrollDelta.y),
        GapWindowMode_Windowed);
  }
}

ecs_comp_define(DemoComp) { u32 tickCount; };

ecs_view_define(UpdateGlobalView) { ecs_access_write(DemoComp); }
ecs_view_define(UpdateWindowView) { ecs_access_write(GapWindowComp); }

ecs_system_define(DemoUpdateSys) {
  EcsView*     globalView = ecs_world_view_t(world, UpdateGlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  DemoComp* demo = ecs_view_write_t(globalItr, DemoComp);
  ++demo->tickCount;

  EcsView* resourceView = ecs_world_view_t(world, UpdateWindowView);
  for (EcsIterator* itr = ecs_view_itr(resourceView); ecs_view_walk(itr);) {
    GapWindowComp* window = ecs_view_write_t(itr, GapWindowComp);
    window_update(world, window, demo->tickCount);
  }
}

ecs_module_init(app_module) {
  ecs_register_comp(DemoComp);

  ecs_register_view(UpdateWindowView);

  ecs_register_system(
      DemoUpdateSys, ecs_register_view(UpdateGlobalView), ecs_register_view(UpdateWindowView));
}

static int demo_run() {
  log_i("Demo starting", log_param("pid", fmt_int(g_thread_pid)));

  EcsDef* def = def = ecs_def_create(g_alloc_heap);
  gap_register(def);
  ecs_register_module(def, app_module);

  EcsWorld*  world  = ecs_world_create(g_alloc_heap, def);
  EcsRunner* runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_DumpGraphDot);

  gap_window_create(world, GapWindowFlags_Default, gap_vector(1024, 768));
  ecs_world_add_t(world, ecs_world_global(world), DemoComp);

  do {
    ecs_run_sync(runner);
    thread_sleep(time_second / 30);
  } while (ecs_utils_any(world, UpdateWindowView));

  log_i("Demo shutdown", log_param("mem", fmt_size(alloc_stats_total())));

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

  CliApp*        app   = cli_app_create(g_alloc_heap, string_lit("Volo Gap Multi-Window Demo"));
  CliInvocation* invoc = cli_parse(app, argc - 1, argv + 1);
  if (cli_parse_result(invoc) == CliParseResult_Fail) {
    cli_failure_write_file(invoc, g_file_stderr);
    exitCode = 2;
    goto exit;
  }

  exitCode = demo_run();

exit:
  cli_parse_destroy(invoc);
  cli_app_destroy(app);

  log_teardown();
  jobs_teardown();
  core_teardown();
  return exitCode;
}
