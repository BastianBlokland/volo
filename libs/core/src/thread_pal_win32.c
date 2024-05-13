#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynlib.h"
#include "core_time.h"
#include "core_winutils.h"

#include "thread_internal.h"

#include <Windows.h>

ASSERT(sizeof(LONG) == sizeof(i32), "Expected LONG to be 32 bit");
ASSERT(sizeof(LONG64) == sizeof(i64), "Expected LONG64 to be 64 bit");

#define thread_early_crash_exit_code 2

/**
 * Requested minimum OS scheduling interval in milliseconds.
 * This is a tradeoff between overhead due to many context switches if set too low and taking a long
 * time to wake threads when set too high.
 */
static const u32 g_win32SchedulingInterval = 2;

static DynLib* g_libMM;
static UINT(SYS_DECL* g_mmTimeBeginPeriod)(UINT period);
static UINT(SYS_DECL* g_mmTimeEndPeriod)(UINT period);

static DynLib* g_libKernel32;
static HRESULT(SYS_DECL* g_setThreadDescription)(HANDLE thread, const wchar_t* description);

/**
 * Crash utility that can be used during early initialization before the allocators and the normal
 * crash infrastructure has been initialized.
 */
static NORETURN void thread_crash_early_init(const String msg) {
  HANDLE stdErr = GetStdHandle(STD_ERROR_HANDLE);
  if (stdErr != INVALID_HANDLE_VALUE) {
    WriteFile(msg.ptr, (DWORD)msg.size, null, null);
  }

  HANDLE curProcess = GetCurrentProcess();
  TerminateProcess(curProcess, diag_crash_exit_code);
  UNREACHABLE
}

static int thread_desired_prio_value(const ThreadPriority prio) {
  switch (prio) {
  case ThreadPriority_Lowest:
    return THREAD_PRIORITY_LOWEST;
  case ThreadPriority_Low:
    return THREAD_PRIORITY_BELOW_NORMAL;
  case ThreadPriority_Normal:
    return THREAD_PRIORITY_NORMAL;
  case ThreadPriority_High:
    return THREAD_PRIORITY_ABOVE_NORMAL;
  case ThreadPriority_Highest:
    return THREAD_PRIORITY_HIGHEST;
  }
  diag_crash_msg("Unsupported thread-priority: {}", fmt_int(prio));
}

MAYBE_UNUSED static void thread_set_process_priority(void) {
  const HANDLE curProcess = GetCurrentProcess();
  if (UNLIKELY(SetPriorityClass(curProcess, ABOVE_NORMAL_PRIORITY_CLASS) == 0)) {
    thread_crash_early_init(string_lit("SetPriorityClass() failed\n"));
  }
}

void thread_pal_init(void) {
  /**
   * NOTE: Called during early startup so cannot allocate memory.
   */
#ifdef VOLO_FAST
  /**
   * When running an optimized build we assume the user wants to give additional priority to the
   * process. We might want to make this customizable in the future.
   * NOTE: Do not raise the priority higher then this to avoid interfering with system functions.
   */
  thread_set_process_priority();
#endif
}

void thread_pal_init_late(void) {
  /**
   * If 'Winmm.dll' (Windows Multimedia API) is available then configure the scheduling interval.
   */
  if (dynlib_load(g_allocPersist, string_lit("Winmm.dll"), &g_libMM) == 0) {
    g_mmTimeBeginPeriod = dynlib_symbol(g_libMM, string_lit("timeBeginPeriod"));
    g_mmTimeEndPeriod   = dynlib_symbol(g_libMM, string_lit("timeEndPeriod"));
  }
  if (g_mmTimeBeginPeriod && g_mmTimeBeginPeriod(g_win32SchedulingInterval)) {
    diag_assert_fail("Failed to set win32 scheduling interval");
  }
  /**
   * 'SetThreadDescription' was introduced in 'Windows 10, version 1607'; optionally load it.
   */
  if (dynlib_load(g_allocPersist, string_lit("kernel32.dll"), &g_libKernel32) == 0) {
    g_setThreadDescription = dynlib_symbol(g_libKernel32, string_lit("SetThreadDescription"));
  }
}

void thread_pal_teardown(void) {
  if (g_mmTimeEndPeriod && g_mmTimeEndPeriod(g_win32SchedulingInterval)) {
    diag_assert_fail("Failed to restore win32 scheduling interval");
  }
  if (g_libMM) {
    dynlib_destroy(g_libMM);
  }
  if (g_libKernel32) {
    dynlib_destroy(g_libKernel32);
  }
}

ASSERT(sizeof(ThreadId) >= sizeof(DWORD), "ThreadId type too small")

ThreadId thread_pal_pid(void) { return (ThreadId)GetCurrentProcessId(); }
ThreadId thread_pal_tid(void) { return (ThreadId)GetCurrentThreadId(); }

u16 thread_pal_core_count(void) {
  SYSTEM_INFO sysInfo;
  GetSystemInfo(&sysInfo);
  return sysInfo.dwNumberOfProcessors;
}

void thread_pal_set_name(const String str) {
  if (!g_setThreadDescription) {
    return; // Thread descriptions are not supported on this windows installation.
  }

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
  const HRESULT res       = g_setThreadDescription(curThread, buffer.ptr);
  if (UNLIKELY(!SUCCEEDED(res))) {
    diag_crash_msg("SetThreadDescription() failed");
  }
}

bool thread_pal_set_priority(const ThreadPriority prio) {
  const int    prioValue = thread_desired_prio_value(prio);
  const HANDLE curThread = GetCurrentThread();
  if (UNLIKELY(SetThreadPriority(curThread, prioValue) == 0)) {
    diag_crash_msg("SetThreadPriority() failed");
  }
  return true; // No elevated permissions requirements on windows.
}

i32 thread_atomic_load_i32(i32* ptr) {
  return InterlockedCompareExchange((volatile LONG*)ptr, 0, 0);
}

u32 thread_atomic_load_u32(u32* ptr) {
  return (u32)InterlockedCompareExchange((volatile LONG*)ptr, 0, 0);
}

i64 thread_atomic_load_i64(i64* ptr) {
  return InterlockedCompareExchange64((volatile LONG64*)ptr, 0, 0);
}

u64 thread_atomic_load_u64(u64* ptr) {
  return (u64)InterlockedCompareExchange64((volatile LONG64*)ptr, 0, 0);
}

void thread_atomic_store_i32(i32* ptr, const i32 value) {
  InterlockedExchange((volatile LONG*)ptr, value);
}

void thread_atomic_store_u32(u32* ptr, const u32 value) {
  InterlockedExchange((volatile LONG*)ptr, (LONG)value);
}

void thread_atomic_store_i64(i64* ptr, const i64 value) {
  InterlockedExchange64((volatile LONG64*)ptr, value);
}

void thread_atomic_store_u64(u64* ptr, const u64 value) {
  InterlockedExchange64((volatile LONG64*)ptr, (LONG64)value);
}

i32 thread_atomic_exchange_i32(i32* ptr, const i32 value) {
  return InterlockedExchange((volatile LONG*)ptr, value);
}

i64 thread_atomic_exchange_i64(i64* ptr, const i64 value) {
  return InterlockedExchange64((volatile LONG64*)ptr, value);
}

bool thread_atomic_compare_exchange_i32(i32* ptr, i32* expected, const i32 value) {
  const i32 read = (i32)InterlockedCompareExchange((volatile LONG*)ptr, value, *expected);
  if (read == *expected) {
    return true;
  }
  *expected = read;
  return false;
}

bool thread_atomic_compare_exchange_i64(i64* ptr, i64* expected, const i64 value) {
  const i64 read = (i64)InterlockedCompareExchange64((volatile LONG64*)ptr, value, *expected);
  if (read == *expected) {
    return true;
  }
  *expected = read;
  return false;
}

i32 thread_atomic_add_i32(i32* ptr, const i32 value) {
  return (i32)InterlockedExchangeAdd((volatile LONG*)ptr, value);
}

i64 thread_atomic_add_i64(i64* ptr, const i64 value) {
  return (i64)InterlockedExchangeAdd64((volatile LONG64*)ptr, value);
}

i32 thread_atomic_sub_i32(i32* ptr, const i32 value) {
  return (i32)InterlockedExchangeAdd((volatile LONG*)ptr, -value);
}

i64 thread_atomic_sub_i64(i64* ptr, i64 value) {
  return (i64)InterlockedExchangeAdd64((volatile LONG64*)ptr, -value);
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

void thread_pal_yield(void) { SwitchToThread(); }

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

bool thread_pal_exists(const ThreadId tid) {
  const HANDLE threadHandle = OpenThread(SYNCHRONIZE, false, (DWORD)tid);
  if (threadHandle) {
    const bool running = WaitForSingleObject(threadHandle, 0) == WAIT_TIMEOUT;
    CloseHandle(threadHandle);
    return running;
  }
  return false;
}

typedef struct {
  CRITICAL_SECTION impl;
  Allocator*       alloc;
} ThreadMutexData;

ThreadMutex thread_mutex_create(Allocator* alloc) {
  ThreadMutexData* data = alloc_alloc_t(alloc, ThreadMutexData);
  data->alloc           = alloc;

  InitializeCriticalSection(&data->impl);
  return (ThreadMutex)data;
}

void thread_mutex_destroy(ThreadMutex handle) {
  ThreadMutexData* data = (ThreadMutexData*)handle;

  DeleteCriticalSection(&data->impl);

  alloc_free_t(data->alloc, data);
}

void thread_mutex_lock(ThreadMutex handle) {
  ThreadMutexData* data = (ThreadMutexData*)handle;

  EnterCriticalSection(&data->impl);
}

bool thread_mutex_trylock(ThreadMutex handle) {
  ThreadMutexData* data = (ThreadMutexData*)handle;

  return TryEnterCriticalSection(&data->impl);
}

void thread_mutex_unlock(ThreadMutex handle) {
  ThreadMutexData* data = (ThreadMutexData*)handle;

  LeaveCriticalSection(&data->impl);
}

typedef struct {
  CONDITION_VARIABLE impl;
  Allocator*         alloc;
} ThreadConditionData;

ThreadCondition thread_cond_create(Allocator* alloc) {
  ThreadConditionData* data = alloc_alloc_t(alloc, ThreadConditionData);
  data->alloc               = alloc;

  InitializeConditionVariable(&data->impl);
  return (ThreadMutex)data;
}

void thread_cond_destroy(ThreadCondition handle) {
  ThreadConditionData* data = (ThreadConditionData*)handle;

  // win32 'CONDITION_VARIABLE' objects do not need to be deleted.

  alloc_free_t(data->alloc, data);
}

void thread_cond_wait(ThreadCondition condHandle, ThreadMutex mutexHandle) {
  ThreadConditionData* condData  = (ThreadConditionData*)condHandle;
  ThreadMutexData*     mutexData = (ThreadMutexData*)mutexHandle;

  const BOOL sleepRes = SleepConditionVariableCS(&condData->impl, &mutexData->impl, INFINITE);
  if (UNLIKELY(!sleepRes)) {
    diag_crash_msg("SleepConditionVariableCS() failed");
  }
}

void thread_cond_signal(ThreadCondition handle) {
  ThreadConditionData* data = (ThreadConditionData*)handle;

  WakeConditionVariable(&data->impl);
}

void thread_cond_broadcast(ThreadCondition handle) {
  ThreadConditionData* data = (ThreadConditionData*)handle;

  WakeAllConditionVariable(&data->impl);
}
