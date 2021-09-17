#include "core_init.h"

#include "jobs_init.h"

#include "log.h"

#include "check_app.h"

int main(const int argc, const char** argv) {
  core_init();
  jobs_init();
  log_init();

  log_add_sink(g_logger, log_sink_json_default(g_alloc_heap, LogMask_All));

  CheckDef* check = check_create(g_alloc_heap);
  register_spec(check, app);
  register_spec(check, failure);
  register_spec(check, help);
  register_spec(check, parse);
  register_spec(check, read);
  register_spec(check, validate);

  const int exitCode = check_app(check, argc, argv);

  check_destroy(check);

  log_teardown();
  jobs_teardown();
  core_teardown();
  return exitCode;
}
