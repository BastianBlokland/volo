#include "core_diag.h"
#include "core_thread.h"
#include "jobs_init.h"

void jobs_init() { diag_assert(g_thread_tid == g_thread_main_tid); }

void jobs_teardown() { diag_assert(g_thread_tid == g_thread_main_tid); }
