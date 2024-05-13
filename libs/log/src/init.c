#include "core_diag.h"
#include "core_thread.h"
#include "log_init.h"

#include "logger_internal.h"

static bool g_initalized;

void log_init(void) {
  diag_assert(g_threadTid == g_threadMainTid);

  if (!g_initalized) {
    g_initalized = true;

    log_global_logger_init();
  }
}

void log_teardown(void) {
  diag_assert(g_threadTid == g_threadMainTid);

  if (g_initalized) {
    g_initalized = false;

    log_global_logger_teardown();
  }
}
