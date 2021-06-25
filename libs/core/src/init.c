#include "core_annotation.h"
#include "core_init.h"
#include "core_thread.h"
#include "core_types.h"

#include "init_internal.h"

static bool              g_intialized;
static THREAD_LOCAL bool g_initialized_thread;

void core_init() {
  if (!g_intialized) {
    g_intialized = true;

    alloc_init();
    time_init();
    file_init();
    tty_init();
    path_init();
    thread_init();
    signal_init();
  }

  if (!g_initialized_thread) {
    alloc_init_thread();
    thread_init_thread();
    rng_init_thread();
    g_initialized_thread = true;
  }
}

void core_teardown() {
  if (g_thread_tid == g_thread_main_tid && g_intialized) {
    g_intialized = false;
    tty_teardown();
  }
  if (g_initialized_thread) {
    alloc_teardown_thread();
    g_initialized_thread = false;
  }
}
