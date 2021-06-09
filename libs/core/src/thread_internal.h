#pragma once
#include "core_thread.h"

#define thread_pal_stacksize (4 * usize_mebibyte)

#if defined(VOLO_LINUX)
#define thread_pal_rettype void*
#elif defined(VOLO_WIN32)
#define thread_pal_rettype unsigned long
#else
_Static_assert(false, "Unsupported platform");
#endif

i64          thread_pal_pid();
i64          thread_pal_tid();
u16          thread_pal_core_count();
void         thread_pal_set_name(String);
ThreadHandle thread_pal_start(thread_pal_rettype (*)(void*), void*);
void         thread_pal_join(ThreadHandle);
