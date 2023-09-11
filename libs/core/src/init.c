#include "core_annotation.h"
#include "core_init.h"
#include "core_thread.h"
#include "core_types.h"

#include "init_internal.h"

static bool              g_intialized;
static THREAD_LOCAL bool g_initializedThread;

void core_init() {
  if (!g_intialized) {
    alloc_init();
    float_init();
    thread_init();
    time_init();
    stringtable_init();
  }

  if (!g_initializedThread) {
    alloc_init_thread();
    thread_init_thread();
    float_init_thread();
    rng_init_thread();
  }

  if (!g_intialized) {
    file_init();
    tty_init();
    path_init();
  }

  g_intialized        = true;
  g_initializedThread = true;
}

void core_teardown() {
  if (g_thread_tid == g_thread_main_tid && g_intialized) {
    stringtable_teardown();
    tty_teardown();
  }
  if (g_initializedThread) {
    alloc_teardown_thread();
    g_initializedThread = false;
  }
  if (g_thread_tid == g_thread_main_tid && g_intialized) {
    thread_teardown();
    alloc_teardown();
    g_intialized = false;
  }
}
