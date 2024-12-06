#include "app_cli.h"
#include "app_ecs.h"
#include "cli_app.h"
#include "cli_parse.h"
#include "cli_read.h"
#include "core_alloc.h"
#include "core_signal.h"
#include "core_thread.h"
#include "ecs_runner.h"
#include "ecs_world.h"
#include "jobs_init.h"
#include "log_sink_json.h"
#include "log_sink_pretty.h"
#include "trace_init.h"
#include "trace_sink_store.h"
#include "trace_sink_superluminal.h"

static CliId              g_optJobWorkers;
static CliId              g_optNoEcsReplan;
MAYBE_UNUSED static CliId g_optTraceNoStore, g_optTraceSl;

void app_cli_configure(CliApp* app) {
  app_ecs_configure(app);

  g_optJobWorkers = cli_register_flag(app, '\0', string_lit("workers"), CliOptionFlags_Value);
  cli_register_desc(app, g_optJobWorkers, string_lit("Amount of job workers."));

  g_optNoEcsReplan = cli_register_flag(app, '\0', string_lit("no-ecs-replan"), 0);
  cli_register_desc(app, g_optNoEcsReplan, string_lit("Disable ecs replanning."));

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

  log_add_sink(g_logger, log_sink_pretty_default(g_allocHeap, LogMask_All));
  log_add_sink(g_logger, log_sink_json_default(g_allocHeap, LogMask_All));

  log_i("Application startup", log_param("pid", fmt_int(g_threadPid)));

#ifdef VOLO_TRACE
  if (!cli_parse_provided(invoc, g_optTraceNoStore)) {
    trace_add_sink(g_tracer, trace_sink_store(g_allocHeap));
  }
  if (cli_parse_provided(invoc, g_optTraceSl)) {
    trace_add_sink(g_tracer, trace_sink_superluminal(g_allocHeap));
  }
#endif

  const JobsConfig jobsConfig = {
      .workerCount = (u16)cli_read_u64(invoc, g_optJobWorkers, 0),
  };
  jobs_init(&jobsConfig);

  // Enable custom signal handling, used for graceful shutdown on interrupt.
  signal_intercept_enable();

  EcsRunnerFlags runnerFlags = EcsRunnerFlags_Replan;
  if (cli_parse_provided(invoc, g_optNoEcsReplan)) {
    runnerFlags &= ~EcsRunnerFlags_Replan;
  }

  EcsDef* def = def = ecs_def_create(g_allocHeap);
  app_ecs_register(def, invoc);

  EcsWorld*  world  = ecs_world_create(g_allocHeap, def);
  EcsRunner* runner = ecs_runner_create(g_allocHeap, world, runnerFlags);
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
