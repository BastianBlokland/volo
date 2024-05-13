#include "core_diag.h"
#include "core_thread.h"
#include "jobs_init.h"

#include "init_internal.h"

static bool g_initalized;

void jobs_init(const JobsConfig* cfg) {
  diag_assert(g_threadTid == g_threadMainTid);

  if (!g_initalized) {
    g_initalized = true;

    scheduler_init();
    executor_init(cfg);
  }
}

void jobs_teardown(void) {
  diag_assert(g_threadTid == g_threadMainTid);

  if (g_initalized) {
    g_initalized = false;

    executor_teardown();
    scheduler_teardown();
  }
}
