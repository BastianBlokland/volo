#include "core_alloc.h"
#include "core_format.h"
#include "core_math.h"
#include "core_thread.h"
#include "net_rest.h"

typedef enum {
  NetRestState_Idle,
  NetRestState_Waiting,
  NetRestState_Busy,
  NetRestState_Finished,
} NetRestState;

typedef struct {
  NetRestState state;
} NetRestRequest;

typedef struct sNetRest {
  Allocator* alloc;

  ThreadMutex     workerMutex;
  ThreadCondition workerWakeCondition;
  ThreadHandle*   workerThreads;
  bool            workerShutdown;
  u32             workerCount;

  NetRestRequest* requests;
  u32             requestCount;
} NetRest;

static void rest_worker_thread(void* data) {
  NetRest* rest = data;

  bool shutdown = false;
  while (!shutdown) {
    thread_mutex_lock(rest->workerMutex);
    {
      thread_cond_wait(rest->workerWakeCondition, rest->workerMutex);
      shutdown = rest->workerShutdown;
    }
    thread_mutex_unlock(rest->workerMutex);
  }
}

NetRest* net_rest_create(Allocator* alloc, u32 workerCount, u32 requestCount) {
  workerCount  = math_max(1, workerCount);
  requestCount = math_max(workerCount, requestCount);

  NetRest* rest = alloc_alloc_t(alloc, NetRest);

  *rest = (NetRest){
      .alloc = alloc,

      .workerMutex         = thread_mutex_create(g_allocHeap),
      .workerWakeCondition = thread_cond_create(g_allocHeap),
      .workerThreads       = alloc_array_t(alloc, ThreadHandle, workerCount),
      .workerCount         = workerCount,

      .requests     = alloc_array_t(alloc, NetRestRequest, requestCount),
      .requestCount = requestCount,
  };

  // Spawn workers.
  for (u32 i = 0; i != workerCount; ++i) {
    const String         threadName = fmt_write_scratch("volo_rest_{}", fmt_int(i));
    const ThreadPriority threadPrio = ThreadPriority_Low;
    rest->workerThreads[i] = thread_start(rest_worker_thread, (void*)rest, threadName, threadPrio);
  }

  return rest;
}

void net_rest_destroy(NetRest* rest) {
  // Signal workers to shutdown.
  thread_mutex_lock(rest->workerMutex);
  {
    rest->workerShutdown = true;
    thread_cond_broadcast(rest->workerWakeCondition);
  }
  thread_mutex_unlock(rest->workerMutex);

  // Wait for workers to shutdown.
  for (u32 i = 0; i != rest->workerCount; ++i) {
    thread_join(rest->workerThreads[i]);
  }

  // Cleanup worker data.
  thread_mutex_destroy(rest->workerMutex);
  thread_mutex_destroy(rest->workerWakeCondition);
  alloc_free_array_t(rest->alloc, rest->workerThreads, rest->workerCount);

  // Cleanup requests.
  alloc_free_array_t(rest->alloc, rest->requests, rest->requestCount);

  alloc_free_t(rest->alloc, rest);
}
