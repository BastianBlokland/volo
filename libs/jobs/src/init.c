#include "core/diag.h"
#include "core/thread.h"
#include "jobs/init.h"

#include "init.h"

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
