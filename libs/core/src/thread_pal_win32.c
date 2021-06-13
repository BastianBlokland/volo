#include "core_diag.h"
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

void thread_pal_set_name(const String str) {
  static const usize maxNameLen = 15;
  if (str.size > maxNameLen) {
    diag_assert_fail(
        &diag_callsite_create(),
        fmt_write_scratch(
            "Thread name '{}' is too long, maximum is {} chars",
            fmt_text(str),
            fmt_int(maxNameLen)));
  }

  const usize bufferSize = winutils_to_widestr_size(str);
  if (sentinel_check(bufferSize)) {
    diag_assert_fail(&diag_callsite_create(), string_lit("Thread name contains invalid utf8"));
  }
  Mem buffer = mem_stack(bufferSize);
  winutils_to_widestr(buffer, str);

  const HANDLE  curThread = GetCurrentThread();
  const HRESULT res       = SetThreadDescription(curThread, buffer.ptr);
  diag_assert_msg(SUCCEEDED(res), string_lit("SetThreadDescription() failed"));
  (void)res;
}

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
  diag_assert_msg(handle, string_lit("CreateThread() failed"));

  _Static_assert(sizeof(ThreadHandle) >= sizeof(HANDLE), "'HANDLE' type too big");
  return (ThreadHandle)handle;
}

void thread_pal_join(ThreadHandle thread) {
  DWORD waitRes = WaitForSingleObject((HANDLE)thread, INFINITE);
  diag_assert_msg(waitRes != WAIT_FAILED, string_lit("WaitForSingleObject() failed"));
  (void)waitRes;

  BOOL closeRes = CloseHandle((HANDLE)thread);
  diag_assert_msg(closeRes, string_lit("CloseHandle() failed"));
  (void)closeRes;
}

void thread_pal_yield() { SwitchToThread(); }

void thread_pal_sleep(const TimeDuration duration) {
  // TODO: This only has milliseconds resolution, investigate alternatives with better resolution.
  Sleep(duration / time_millisecond);
}

typedef struct {
  Allocator* alloc;
  Mem        allocation;
} ThreadMutexExtraData;

ThreadMutex thread_pal_mutex_create(Allocator* alloc) {
  Mem allocation = alloc_alloc(alloc, sizeof(CRITICAL_SECTION) + sizeof(ThreadMutexExtraData));
  CRITICAL_SECTION* critSection = mem_as_t(allocation, CRITICAL_SECTION);

  *mem_as_t(mem_consume(allocation, sizeof(CRITICAL_SECTION)), ThreadMutexExtraData) =
      (ThreadMutexExtraData){
          .alloc      = alloc,
          .allocation = allocation,
      };

  InitializeCriticalSection(critSection);
  return (ThreadMutex)allocation.ptr;
}

void thread_pal_mutex_destroy(ThreadMutex handle) {
  CRITICAL_SECTION* critSection = (CRITICAL_SECTION*)handle;

  DeleteCriticalSection(critSection);

  ThreadMutexExtraData* extraData = (ThreadMutexExtraData*)((u8*)handle + sizeof(CRITICAL_SECTION));
  alloc_free(extraData->alloc, extraData->allocation);
}

void thread_pal_mutex_lock(ThreadMutex handle) {
  CRITICAL_SECTION* critSection = (CRITICAL_SECTION*)handle;

  EnterCriticalSection(critSection);
}

bool thread_pal_mutex_trylock(ThreadMutex handle) {
  CRITICAL_SECTION* critSection = (CRITICAL_SECTION*)handle;

  return TryEnterCriticalSection(critSection);
}

void thread_pal_mutex_unlock(ThreadMutex handle) {
  CRITICAL_SECTION* critSection = (CRITICAL_SECTION*)handle;

  LeaveCriticalSection(critSection);
}

typedef struct {
  Allocator* alloc;
  Mem        allocation;
} ThreadConditionExtraData;

ThreadCondition thread_pal_cond_create(Allocator* alloc) {
  Mem allocation = alloc_alloc(alloc, sizeof(CONDITION_VARIABLE) + sizeof(ThreadMutexExtraData));
  CONDITION_VARIABLE* condVar = mem_as_t(allocation, CONDITION_VARIABLE);

  *mem_as_t(mem_consume(allocation, sizeof(CONDITION_VARIABLE)), ThreadMutexExtraData) =
      (ThreadMutexExtraData){
          .alloc      = alloc,
          .allocation = allocation,
      };

  InitializeConditionVariable(condVar);
  return (ThreadMutex)allocation.ptr;
}

void thread_pal_cond_destroy(ThreadCondition handle) {

  // win32 'CONDITION_VARIABLE' objects do not need to be deleted.

  ThreadMutexExtraData* extraData =
      (ThreadMutexExtraData*)((u8*)handle + sizeof(CONDITION_VARIABLE));
  alloc_free(extraData->alloc, extraData->allocation);
}

void thread_pal_cond_wait(ThreadCondition condHandle, ThreadMutex mutexHandle) {
  CONDITION_VARIABLE* condVar     = (CONDITION_VARIABLE*)condHandle;
  CRITICAL_SECTION*   critSection = (CRITICAL_SECTION*)mutexHandle;

  BOOL sleepRes = SleepConditionVariableCS(condVar, critSection, INFINITE);
  diag_assert_msg(sleepRes, string_lit("SleepConditionVariableCS() failed"));
  (void)sleepRes;
}

void thread_pal_cond_signal(ThreadCondition handle) {
  CONDITION_VARIABLE* condVar = (CONDITION_VARIABLE*)handle;

  WakeConditionVariable(condVar);
}

void thread_pal_cond_broadcast(ThreadCondition handle) {
  CONDITION_VARIABLE* condVar = (CONDITION_VARIABLE*)handle;

  WakeAllConditionVariable(condVar);
}
