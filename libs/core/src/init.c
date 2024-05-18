#include "core_annotation.h"
#include "core_init.h"
#include "core_thread.h"
#include "core_types.h"

#include "init_internal.h"

static bool              g_initalized;
static THREAD_LOCAL bool g_initializedThread;

void core_init(void) {
  if (!g_initalized) {
    thread_init();
    float_init();
  }
  if (!g_initializedThread) {
    thread_init_thread();
    float_init_thread();
  }
  if (!g_initalized) {
    alloc_init();
  }
  if (!g_initializedThread) {
    alloc_init_thread();
  }
  if (!g_initalized) {
    symbol_init();
    time_init();
  }
  if (!g_initializedThread) {
    rng_init_thread();
  }
  if (!g_initalized) {
    stringtable_init();
    file_init();
    tty_init();
    path_init();
    dynlib_init();
    thread_init_late();
  }

  g_initalized        = true;
  g_initializedThread = true;
}

void core_teardown(void) {
  if (g_threadTid == g_threadMainTid && g_initalized) {
    stringtable_teardown(); // Teardown early as it contains heap allocations.

    dynlib_leak_report();
    file_leak_report();
    alloc_leak_report();
  }
  if (g_initializedThread) {
    alloc_teardown_thread();
    g_initializedThread = false;
  }
  if (g_threadTid == g_threadMainTid && g_initalized) {
    thread_teardown();
    symbol_teardown();
    alloc_teardown();
    tty_teardown();
    g_initalized = false;
  }
}
