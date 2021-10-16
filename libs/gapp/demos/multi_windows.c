#include "cli.h"
#include "core_alloc.h"
#include "core_file.h"
#include "core_init.h"
#include "core_signal.h"
#include "core_thread.h"
#include "core_time.h"
#include "ecs.h"
#include "jobs.h"
#include "log.h"

static int run_app() {
  EcsDef* def = def = ecs_def_create(g_alloc_heap);

  EcsWorld*  world  = ecs_world_create(g_alloc_heap, def);
  EcsRunner* runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_DumpGraphDot);

  while (!signal_is_received(Signal_Interupt)) {
    ecs_run_sync(runner);
    thread_sleep(time_second / 30);
  }

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

  CliApp*        app   = cli_app_create(g_alloc_heap, string_lit("Volo Multi-Windows Demo"));
  CliInvocation* invoc = cli_parse(app, argc - 1, argv + 1);
  if (cli_parse_result(invoc) == CliParseResult_Fail) {
    cli_failure_write_file(invoc, g_file_stderr);
    exitCode = 2;
    goto exit;
  }

  log_i("App startup");

  exitCode = run_app();

  log_i("App shutdown");

exit:
  cli_parse_destroy(invoc);
  cli_app_destroy(app);

  log_teardown();
  jobs_teardown();
  core_teardown();
  return exitCode;
}
