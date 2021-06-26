#include "core_alloc.h"
#include "core_init.h"

#include "jobs_init.h"

#include "check_runner.h"

int main() {
  core_init();
  jobs_init();

  CheckDef* check = check_create(g_alloc_heap);

  register_spec(check, a);
  register_spec(check, b);

  const CheckRunResult res = check_run(check);
  check_destroy(check);

  jobs_teardown();
  core_teardown();
  return res;
}
