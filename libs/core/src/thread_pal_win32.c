#include "core_alloc.h"
#include "core_diag.h"
#include "core_time.h"
#include "core_winutils.h"

#include "thread_internal.h"

#include <Windows.h>

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
   * affecting the program behaviour.
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

i64 thread_pal_atomic_load_i64(i64* ptr) {
  return InterlockedCompareExchange64((volatile i64*)ptr, 0, 0);
}

void thread_pal_atomic_store_i64(i64* ptr, i64 value) {
  InterlockedExchange64((volatile i64*)ptr, value);
}

i64 thread_pal_atomic_exchange_i64(i64* ptr, i64 value) {
  return InterlockedExchange64((volatile i64*)ptr, value);
}

bool thread_pal_atomic_compare_exchange_i64(i64* ptr, i64* expected, i64 value) {
  const i64 read = (i64)InterlockedCompareExchange64((volatile i64*)ptr, value, *expected);
  if (read == *expected) {
    return true;
  }
  *expected = read;
  return false;
}

i64 thread_pal_atomic_add_i64(i64* ptr, i64 value) {
  i64 current;
  i64 add;
  do {
    current = *ptr;
    add     = current + value;
  } while (InterlockedCompareExchange64(ptr, add, current) != current);
  return current;
}

i64 thread_pal_atomic_sub_i64(i64* ptr, i64 value) {
  i64 current;
  i64 sub;
  do {
    current = *ptr;
    sub     = current - value;
  } while (InterlockedCompareExchange64(ptr, sub, current) != current);
  return current;
}

ThreadHandle thread_pal_start(thread_pal_rettype (*routine)(void*), void* data) {
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
  static const TimeDuration g_minSleep = time_milliseconds(15);

  /**
   * On Win32 Sleep() only has granularity to about 10 - 15ms due to the default scheduling period.
   * To sill provide support for short sleeps we do the bulk of the waiting using Sleep() and then
   * do a loop of yielding our timeslice until the desired duration is met.
   *
   * TODO: Is it worth it to change the global scheduling period?
   * Requires linking with the Winmm dll.
   * https://docs.microsoft.com/en-us/windows/win32/api/timeapi/nf-timeapi-timebeginperiod
   */
  TimeSteady start = time_steady_clock();

  // Bulk of the sleeping.
  if (duration > g_minSleep) {
    Sleep((DWORD)((duration - g_minSleep) / time_millisecond));
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
