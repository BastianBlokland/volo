#include "core_init.h"
#include "core_types.h"

static bool g_intialized;

void alloc_init();
void time_init();
void file_init();
void tty_init();
void path_init();
void thread_init();
void signal_init();

void tty_teardown();

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
}

void core_teardown() {
  if (g_intialized) {
    g_intialized = false;
    tty_teardown();
  }
}
