#include "core_annotation.h"
#include "core_init.h"
#include "core_thread.h"
#include "core_types.h"

#include "init_internal.h"

static bool              g_intialized;
static THREAD_LOCAL bool g_initialized_thread;

void core_init() {
  if (!g_intialized) {
    bits_init();
    alloc_init();
    thread_init();
    time_init();
  }

  if (!g_initialized_thread) {
    alloc_init_thread();
    thread_init_thread();
    rng_init_thread();
  }

  if (!g_intialized) {
    file_init();
    tty_init();
    signal_init();
    path_init();
  }

  g_intialized         = true;
  g_initialized_thread = true;
}

void core_teardown() {
  if (g_thread_tid == g_thread_main_tid && g_intialized) {
    tty_teardown();
  }
  if (g_initialized_thread) {
    alloc_teardown_thread();
    g_initialized_thread = false;
  }
  if (g_thread_tid == g_thread_main_tid && g_intialized) {
    alloc_teardown();
    g_intialized = false;
  }
}
