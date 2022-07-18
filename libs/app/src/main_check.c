#include "app_check.h"
#include "check_app.h"
#include "core.h"
#include "jobs.h"
#include "log.h"

/**
 * Entrypoint for check (unit test library) applications.
 */
int main(const int argc, const char** argv) {
  core_init();
  jobs_init();
  log_init();

  log_add_sink(g_logger, log_sink_json_default(g_alloc_heap, LogMask_All));

  CheckDef* check = check_create(g_alloc_heap);
  app_check_configure(check);

  const int exitCode = check_app(check, argc, argv);

  check_destroy(check);

  log_teardown();
  jobs_teardown();
  core_teardown();
  return exitCode;
}
