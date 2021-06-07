#include "core_thread.h"
#include "thread_internal.h"

void thread_init() { thread_pal_name_current(string_lit("volo_main")); }
