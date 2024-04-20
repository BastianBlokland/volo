#include "app_cli.h"
#include "app_ecs.h"
#include "core_alloc.h"
#include "core_signal.h"
#include "core_thread.h"
#include "ecs_runner.h"
#include "ecs_world.h"
#include "jobs_init.h"
#include "log.h"
#include "trace.h"

MAYBE_UNUSED static CliId g_optTraceNoStore, g_optTraceSl;

void app_cli_configure(CliApp* app) {
  app_ecs_configure(app);

#ifdef VOLO_TRACE
  g_optTraceNoStore = cli_register_flag(app, '\0', string_lit("trace-no-store"), 0);
  cli_register_desc(app, g_optTraceNoStore, string_lit("Disable the trace store sink."));

  g_optTraceSl = cli_register_flag(app, '\0', string_lit("trace-sl"), 0);
  cli_register_desc(app, g_optTraceSl, string_lit("Enable the SuperLuminal trace sink."));
#endif
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  trace_init();

  i32 exitCode = 0;
  if (!app_ecs_validate(app, invoc)) {
    exitCode = 1;
    goto Exit;
  }

  log_add_sink(g_logger, log_sink_pretty_default(g_alloc_heap, LogMask_All));
  log_add_sink(g_logger, log_sink_json_default(g_alloc_heap, LogMask_All));

  log_i("Application startup", log_param("pid", fmt_int(g_thread_pid)));

#ifdef VOLO_TRACE
  if (!cli_parse_provided(invoc, g_optTraceNoStore)) {
    trace_add_sink(g_tracer, trace_sink_store(g_alloc_heap));
  }
  if (cli_parse_provided(invoc, g_optTraceSl)) {
    trace_add_sink(g_tracer, trace_sink_superluminal(g_alloc_heap));
  }
#endif

  jobs_init();

  // Enable custom signal handling, used for graceful shutdown on interrupt.
  signal_intercept_enable();

  EcsDef* def = def = ecs_def_create(g_alloc_heap);
  app_ecs_register(def, invoc);

  EcsWorld*  world  = ecs_world_create(g_alloc_heap, def);
  EcsRunner* runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_DumpGraphDot);
  app_ecs_init(world, invoc);

  ecs_world_flush(world); // Flush any entity / component additions made during the init.

  u64 frameNumber = 0;
  do {
    trace_begin_msg("app_frame", TraceColor_Blue, "frame-{}", fmt_int(frameNumber));

    ecs_run_sync(runner);

    trace_end();
    ++frameNumber;
  } while (!app_ecs_should_quit(world));

  ecs_runner_destroy(runner);
  ecs_world_destroy(world);
  ecs_def_destroy(def);

  log_i("Application shutdown");

Exit:
  jobs_teardown();
  trace_teardown();
  return exitCode;
}
