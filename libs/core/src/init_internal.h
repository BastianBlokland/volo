#pragma once
#include "core_init.h"

/**
 * Global initialization routines.
 * Fired once when the core library is initialized.
 */
void alloc_init(void);
void file_init(void);
void float_init(void);
void path_init(void);
void thread_init(void);
void thread_init_late(void);
void time_init(void);
void tty_init(void);
void stringtable_init(void);
void symbol_init(void);
void dynlib_init(void);

/**
 * Thread initialization routines.
 * Fired once for every thread the core library is initialized on.
 */
void alloc_init_thread(void);
void thread_init_thread(void);
void float_init_thread(void);
void rng_init_thread(void);

/**
 * Global teardown routines.
 * Fired once when the core library is torn down.
 */
void alloc_teardown(void);
void thread_teardown(void);
void tty_teardown(void);
void stringtable_teardown(void);
void symbol_teardown();
void dynlib_teardown(void);
void file_teardown(void);

/**
 * Thread teardown routines.
 * Fired once for every thread when its being torn down.
 */
void alloc_teardown_thread(void);
