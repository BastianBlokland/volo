#include "core_alloc.h"
#include "core_thread.h"
#include "thread_internal.h"

typedef struct {
  Mem           allocation;
  String        threadName;
  ThreadRoutine userRoutine;
  void*         userData;
} ThreadRunData;

static thread_pal_rettype thread_runner(void* data) {
  ThreadRunData* runData = (ThreadRunData*)data;

  // Initialize thread data.
  g_thread_tid  = thread_pal_tid();
  g_thread_name = runData->threadName;
  thread_pal_set_name(g_thread_name);

  // Invoke the user routine.
  runData->userRoutine(runData->userData);

  // Cleanup thread data.
  alloc_free(g_alloc_heap, runData->allocation);

  return null;
}

i64                 g_thread_pid;
i64                 g_thread_main_tid;
THREAD_LOCAL i64    g_thread_tid;
THREAD_LOCAL String g_thread_name;
u16                 g_thread_core_count;

void thread_init() {
  g_thread_pid        = thread_pal_pid();
  g_thread_tid        = thread_pal_tid();
  g_thread_main_tid   = g_thread_tid;
  g_thread_name       = string_lit("volo_main");
  g_thread_core_count = thread_pal_core_count();
  thread_pal_set_name(g_thread_name);
}

i64 thread_atomic_load_i64(i64* ptr) { return thread_pal_atomic_load_i64(ptr); }

void thread_atomic_store_i64(i64* ptr, i64 value) { thread_pal_atomic_store_i64(ptr, value); }

i64 thread_atomic_exchange_i64(i64* ptr, i64 value) {
  return thread_pal_atomic_exchange_i64(ptr, value);
}

bool thread_atomic_compare_exchange_i64(i64* ptr, i64* expected, i64 value) {
  return thread_pal_atomic_compare_exchange_i64(ptr, expected, value);
}

i64 thread_atomic_add_i64(i64* ptr, i64 value) { return thread_pal_atomic_add_i64(ptr, value); }

i64 thread_atomic_sub_i64(i64* ptr, i64 value) { return thread_pal_atomic_sub_i64(ptr, value); }

ThreadHandle thread_start(ThreadRoutine routine, void* data, String threadName) {
  Mem allocation = alloc_alloc(g_alloc_heap, sizeof(ThreadRunData) + threadName.size);
  *(ThreadRunData*)allocation.ptr = (ThreadRunData){
      .allocation  = allocation,
      .threadName  = mem_create((u8*)allocation.ptr + sizeof(ThreadRunData), threadName.size),
      .userRoutine = routine,
      .userData    = data,
  };
  mem_cpy(((ThreadRunData*)allocation.ptr)->threadName, threadName);

  return thread_pal_start(thread_runner, allocation.ptr);
}

void thread_join(ThreadHandle thread) { thread_pal_join(thread); }

void thread_yield() { thread_pal_yield(); }

void thread_sleep(const TimeDuration duration) { thread_pal_sleep(duration); }

ThreadMutex thread_mutex_create(Allocator* alloc) { return thread_pal_mutex_create(alloc); }

void thread_mutex_destroy(ThreadMutex mutex) { thread_pal_mutex_destroy(mutex); }

void thread_mutex_lock(ThreadMutex mutex) { thread_pal_mutex_lock(mutex); }

bool thread_mutex_trylock(ThreadMutex mutex) { return thread_pal_mutex_trylock(mutex); }

void thread_mutex_unlock(ThreadMutex mutex) { thread_pal_mutex_unlock(mutex); }
