#include "check_app.h"
#include "core_alloc.h"
#include "core_init.h"
#include "jobs.h"
#include "log.h"

int main(const int argc, const char** argv) {
  core_init();
  jobs_init();
  log_init();

  log_add_sink(g_logger, log_sink_json_default(g_alloc_heap, LogMask_All));

  CheckDef* check = check_create(g_alloc_heap);
  register_spec(check, affinity);
  register_spec(check, def);
  register_spec(check, destruct);
  register_spec(check, entity);
  register_spec(check, graph);
  register_spec(check, runner);
  register_spec(check, storage);
  register_spec(check, utils);
  register_spec(check, view);
  register_spec(check, world);

  const int exitCode = check_app(check, argc, argv);

  check_destroy(check);

  log_teardown();
  jobs_teardown();
  core_teardown();
  return exitCode;
}
