#include "core_diag.h"
#include "core_thread.h"
#include "trace_init.h"

#include "tracer_internal.h"

static bool g_initalized;

void trace_init(void) {
  diag_assert(g_threadTid == g_threadMainTid);

  if (!g_initalized) {
    g_initalized = true;

    trace_global_tracer_init();
  }
}

void trace_teardown(void) {
  diag_assert(g_threadTid == g_threadMainTid);

  if (g_initalized) {
    g_initalized = false;

    trace_global_tracer_teardown();
  }
}
