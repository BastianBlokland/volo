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
u16                 g_thread_cpu_count;

void thread_init() {
  g_thread_pid       = thread_pal_pid();
  g_thread_tid       = thread_pal_tid();
  g_thread_main_tid  = g_thread_tid;
  g_thread_name      = string_lit("volo_main");
  g_thread_cpu_count = thread_pal_processor_count();
  thread_pal_set_name(g_thread_name);
}

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
