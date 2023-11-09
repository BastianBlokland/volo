#include "core_alloc.h"
#include "core_diag.h"
#include "core_time.h"
#include "core_winutils.h"

#include "thread_internal.h"

#include <Windows.h>
#include <mmsystem.h> // Part of the Windows Multimedia API (winmm.lib).

ASSERT(sizeof(LONG) == sizeof(i32), "Expected LONG to be 32 bit");
ASSERT(sizeof(LONG64) == sizeof(i64), "Expected LONG64 to be 64 bit");

/**
 * Requested minimum OS scheduling interval in milliseconds.
 * This is a tradeoff between overhead due to many context switches if set too low and taking a long
 * time to wake threads when set too high.
 */
static const u32 g_win32SchedulingInterval = 2;

void thread_pal_init() {
  if (timeBeginPeriod(g_win32SchedulingInterval) != TIMERR_NOERROR) {
    diag_assert_fail("Failed to set win32 scheduling interval");
  }
}

void thread_pal_teardown() {
  if (timeEndPeriod(g_win32SchedulingInterval) != TIMERR_NOERROR) {
    diag_assert_fail("Failed to restore win32 scheduling interval");
  }
}

i64 thread_pal_pid() { return GetCurrentProcessId(); }
i64 thread_pal_tid() { return GetCurrentThreadId(); }

u16 thread_pal_core_count() {
  SYSTEM_INFO sysInfo;
  GetSystemInfo(&sysInfo);
  return sysInfo.dwNumberOfProcessors;
}

#if defined(__MINGW32__)
void thread_pal_set_name(const String str) {
  /**
   * Under MinGW (Minimalist GNU for Windows) (GNU gcc + GNU binutils port to windows), the
   * 'SetThreadDescription' win32 api is current not supported. Seeing that 'thread_pal_set_name()'
   * is a convenience api for use during debugging / profiling we can simply stub it out without
   * affecting the program behavior.
   */
  (void)str;
}
#else
void thread_pal_set_name(const String str) {
  static const usize g_maxNameLen = 15;
  if (str.size > g_maxNameLen) {
    diag_assert_fail(
        "Thread name '{}' is too long, maximum is {} chars", fmt_text(str), fmt_int(g_maxNameLen));
  }

  const usize bufferSize = winutils_to_widestr_size(str);
  if (sentinel_check(bufferSize)) {
    diag_assert_fail("Thread name contains invalid utf8");
  }
  Mem buffer = mem_stack(bufferSize);
  winutils_to_widestr(buffer, str);

  const HANDLE  curThread = GetCurrentThread();
  const HRESULT res       = SetThreadDescription(curThread, buffer.ptr);
  if (UNLIKELY(!SUCCEEDED(res))) {
    diag_crash_msg("SetThreadDescription() failed");
  }
}
#endif // !defined(__MINGW32__)

i32 thread_pal_atomic_load_i32(i32* ptr) {
  return InterlockedCompareExchange((volatile LONG*)ptr, 0, 0);
}

i64 thread_pal_atomic_load_i64(i64* ptr) {
  return InterlockedCompareExchange64((volatile LONG64*)ptr, 0, 0);
}

void thread_pal_atomic_store_i32(i32* ptr, const i32 value) {
  InterlockedExchange((volatile LONG*)ptr, value);
}

void thread_pal_atomic_store_i64(i64* ptr, const i64 value) {
  InterlockedExchange64((volatile LONG64*)ptr, value);
}

i32 thread_pal_atomic_exchange_i32(i32* ptr, const i32 value) {
  return InterlockedExchange((volatile LONG*)ptr, value);
}

i64 thread_pal_atomic_exchange_i64(i64* ptr, const i64 value) {
  return InterlockedExchange64((volatile LONG64*)ptr, value);
}

bool thread_pal_atomic_compare_exchange_i32(i32* ptr, i32* expected, const i32 value) {
  const i32 read = (i32)InterlockedCompareExchange((volatile LONG*)ptr, value, *expected);
  if (read == *expected) {
    return true;
  }
  *expected = read;
  return false;
}

bool thread_pal_atomic_compare_exchange_i64(i64* ptr, i64* expected, const i64 value) {
  const i64 read = (i64)InterlockedCompareExchange64((volatile LONG64*)ptr, value, *expected);
  if (read == *expected) {
    return true;
  }
  *expected = read;
  return false;
}

i32 thread_pal_atomic_add_i32(i32* ptr, const i32 value) {
  i32 current;
  i32 add;
  do {
    current = *ptr;
    add     = current + value;
  } while (InterlockedCompareExchange((volatile LONG*)ptr, add, current) != current);
  return current;
}

i64 thread_pal_atomic_add_i64(i64* ptr, const i64 value) {
  i64 current;
  i64 add;
  do {
    current = *ptr;
    add     = current + value;
  } while (InterlockedCompareExchange64((volatile LONG64*)ptr, add, current) != current);
  return current;
}

i32 thread_pal_atomic_sub_i32(i32* ptr, const i32 value) {
  i32 current;
  i32 sub;
  do {
    current = *ptr;
    sub     = current - value;
  } while (InterlockedCompareExchange((volatile LONG*)ptr, sub, current) != current);
  return current;
}

i64 thread_pal_atomic_sub_i64(i64* ptr, i64 value) {
  i64 current;
  i64 sub;
  do {
    current = *ptr;
    sub     = current - value;
  } while (InterlockedCompareExchange64((volatile LONG64*)ptr, sub, current) != current);
  return current;
}

ThreadHandle thread_pal_start(thread_pal_rettype(SYS_DECL* routine)(void*), void* data) {
  HANDLE handle = CreateThread(null, thread_pal_stacksize, routine, data, 0, null);
  if (UNLIKELY(!handle)) {
    diag_crash_msg("CreateThread() failed");
  }
  ASSERT(sizeof(ThreadHandle) >= sizeof(HANDLE), "'HANDLE' type too big");
  return (ThreadHandle)handle;
}

void thread_pal_join(ThreadHandle thread) {
  DWORD waitRes = WaitForSingleObject((HANDLE)thread, INFINITE);
  if (UNLIKELY(waitRes == WAIT_FAILED)) {
    diag_crash_msg("WaitForSingleObject() failed");
  }

  BOOL closeRes = CloseHandle((HANDLE)thread);
  if (UNLIKELY(!closeRes)) {
    diag_crash_msg("CloseHandle() failed");
  }
}

void thread_pal_yield() { SwitchToThread(); }

void thread_pal_sleep(const TimeDuration duration) {
  /**
   * On Win32 Sleep() only has granularity up to the scheduling period.
   * To sill provide support for short sleeps we do the bulk of the waiting using Sleep() and then
   * do a loop of yielding our timeslice until the desired duration is met.
   */
  TimeSteady start = time_steady_clock();

  // Bulk of the sleeping.
  if (duration > time_milliseconds(g_win32SchedulingInterval)) {
    Sleep((DWORD)((duration - time_milliseconds(g_win32SchedulingInterval)) / time_millisecond));
  }

  // Wait for the remaining time by yielding our timeslice.
  while (duration > time_steady_duration(start, time_steady_clock())) {
    thread_pal_yield();
  }
}

typedef struct {
  CRITICAL_SECTION impl;
  Allocator*       alloc;
} ThreadMutexData;

ThreadMutex thread_pal_mutex_create(Allocator* alloc) {
  ThreadMutexData* data = alloc_alloc_t(alloc, ThreadMutexData);
  data->alloc           = alloc;

  InitializeCriticalSection(&data->impl);
  return (ThreadMutex)data;
}

void thread_pal_mutex_destroy(ThreadMutex handle) {
  ThreadMutexData* data = (ThreadMutexData*)handle;

  DeleteCriticalSection(&data->impl);

  alloc_free_t(data->alloc, data);
}

void thread_pal_mutex_lock(ThreadMutex handle) {
  ThreadMutexData* data = (ThreadMutexData*)handle;

  EnterCriticalSection(&data->impl);
}

bool thread_pal_mutex_trylock(ThreadMutex handle) {
  ThreadMutexData* data = (ThreadMutexData*)handle;

  return TryEnterCriticalSection(&data->impl);
}

void thread_pal_mutex_unlock(ThreadMutex handle) {
  ThreadMutexData* data = (ThreadMutexData*)handle;

  LeaveCriticalSection(&data->impl);
}

typedef struct {
  CONDITION_VARIABLE impl;
  Allocator*         alloc;
} ThreadConditionData;

ThreadCondition thread_pal_cond_create(Allocator* alloc) {
  ThreadConditionData* data = alloc_alloc_t(alloc, ThreadConditionData);
  data->alloc               = alloc;

  InitializeConditionVariable(&data->impl);
  return (ThreadMutex)data;
}

void thread_pal_cond_destroy(ThreadCondition handle) {
  ThreadConditionData* data = (ThreadConditionData*)handle;

  // win32 'CONDITION_VARIABLE' objects do not need to be deleted.

  alloc_free_t(data->alloc, data);
}

void thread_pal_cond_wait(ThreadCondition condHandle, ThreadMutex mutexHandle) {
  ThreadConditionData* condData  = (ThreadConditionData*)condHandle;
  ThreadMutexData*     mutexData = (ThreadMutexData*)mutexHandle;

  const BOOL sleepRes = SleepConditionVariableCS(&condData->impl, &mutexData->impl, INFINITE);
  if (UNLIKELY(!sleepRes)) {
    diag_crash_msg("SleepConditionVariableCS() failed");
  }
}

void thread_pal_cond_signal(ThreadCondition handle) {
  ThreadConditionData* data = (ThreadConditionData*)handle;

  WakeConditionVariable(&data->impl);
}

void thread_pal_cond_broadcast(ThreadCondition handle) {
  ThreadConditionData* data = (ThreadConditionData*)handle;

  WakeAllConditionVariable(&data->impl);
}
