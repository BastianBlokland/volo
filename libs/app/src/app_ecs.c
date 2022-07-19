#include "app_cli.h"
#include "app_ecs.h"
#include "core_alloc.h"
#include "core_thread.h"
#include "ecs_runner.h"
#include "ecs_world.h"
#include "log.h"

void app_cli_configure(CliApp* app) { app_ecs_configure(app); }

i32 app_cli_run(MAYBE_UNUSED const CliApp* app, const CliInvocation* invoc) {

  log_add_sink(g_logger, log_sink_pretty_default(g_alloc_heap, LogMask_All));
  log_add_sink(g_logger, log_sink_json_default(g_alloc_heap, LogMask_All));

  log_i("Application startup", log_param("pid", fmt_int(g_thread_pid)));

  EcsDef* def = def = ecs_def_create(g_alloc_heap);
  app_ecs_register(def, invoc);

  EcsWorld*  world  = ecs_world_create(g_alloc_heap, def);
  EcsRunner* runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_DumpGraphDot);
  app_ecs_init(world, invoc);

  do {
    ecs_run_sync(runner);
  } while (!app_ecs_should_quit(world));

  ecs_runner_destroy(runner);
  ecs_world_destroy(world);
  ecs_def_destroy(def);

  log_i("Application shutdown");

  return 0;
}
