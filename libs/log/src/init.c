#include "core_diag.h"
#include "core_thread.h"

#include "log_init.h"

#include "logger_internal.h"

static bool g_intialized;

void log_init() {
  diag_assert(g_thread_tid == g_thread_main_tid);

  if (!g_intialized) {
    g_intialized = true;

    log_global_logger_init();
  }
}

void log_teardown() {
  diag_assert(g_thread_tid == g_thread_main_tid);

  if (g_intialized) {
    g_intialized = false;

    log_global_logger_teardown();
  }
}
