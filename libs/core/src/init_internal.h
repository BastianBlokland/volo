#pragma once
#include "core_init.h"

/**
 * Global initialization routines.
 * Fired once when the core library is initialized.
 */
void alloc_init();
void bits_init();
void file_init();
void path_init();
void signal_init();
void thread_init();
void time_init();
void tty_init();

/**
 * Thread initialization routines.
 * Fired once for every thread the core library is initialized on.
 */
void alloc_init_thread();
void rng_init_thread();
void thread_init_thread();

/**
 * Global teardown routines.
 * Fired once when the core library is torn down.
 */
void alloc_teardown();
void tty_teardown();

/**
 * Thread teardown routines.
 * Fired once for every thread when its being torn down.
 */
void alloc_teardown_thread();
