#include "core_diag.h"
#include "core_dynarray.h"
#include "core_format.h"
#include "core_math.h"
#include "core_thread.h"
#include "executor_internal.h"

// Amounts of cores reserved for OS and other applications on the system.
#define worker_reserved_core_count 1
#define worker_min_count 1
#define worker_max_count 64

typedef enum {
  ExecMode_Running,
  ExecMode_Teardown,
} ExecMode;

typedef struct {
  int placeholder;
} WorkerData;

static ExecMode        g_mode = ExecMode_Running; // Change only while holding 'g_mutex'.
static ThreadHandle    g_workerThreads[worker_max_count];
static WorkerData      g_workerData[worker_max_count];
static ThreadMutex     g_mutex;
static ThreadCondition g_wakeCondition;

u16                      g_jobsWorkerCount;
THREAD_LOCAL JobWorkerId g_jobsWorkerId;
THREAD_LOCAL bool        g_jobsIsWorker;

static void executor_thread(void* data) {
  g_jobsWorkerId = (JobWorkerId)(usize)data;
  g_jobsIsWorker = true;

  while (true) {
    thread_mutex_lock(g_mutex);

  Sleep:
    if (UNLIKELY(g_mode != ExecMode_Running)) {
      thread_mutex_unlock(g_mutex);
      break;
    }
    thread_cond_wait(g_wakeCondition, g_mutex);
    goto Sleep;
  }
}

void executor_init() {
  g_jobsWorkerCount = math_min(
      math_max(worker_min_count, g_thread_core_count - worker_reserved_core_count),
      worker_max_count);
  g_mutex         = thread_mutex_create(g_alloc_heap);
  g_wakeCondition = thread_cond_create(g_alloc_heap);

  for (u16 i = 0; i != g_jobsWorkerCount; ++i) {
    // Init resources: g_workerData[i]
  }

  // Setup worker info for the main-thread.
  g_jobsWorkerId = 0;
  g_jobsIsWorker = true;

  // Start threads for the other workers.
  for (u16 i = 1; i != g_jobsWorkerCount; ++i) {
    g_workerThreads[i] = thread_start(
        executor_thread, (void*)(usize)i, fmt_write_scratch("jobs_exec_{}", fmt_int(i)));
  }
}

void executor_teardown() {
  diag_assert_msg(
      g_thread_tid == g_thread_main_tid, "Only the main-thread can teardown the executor");
  diag_assert_msg(g_jobsWorkerId == 0, "Unexpected worker-id for the main-thread");

  // Signal the workers for teardown.
  thread_mutex_lock(g_mutex);
  g_mode = ExecMode_Teardown;
  thread_cond_broadcast(g_wakeCondition);
  thread_mutex_unlock(g_mutex);

  // Wait for all worker threads to stop.
  for (u16 i = 1; i != g_jobsWorkerCount; ++i) {
    thread_join(g_workerThreads[i]);
  }

  for (u16 i = 0; i != g_jobsWorkerCount; ++i) {
    // Cleanup resources: g_workerData[i]
  }

  thread_cond_destroy(g_wakeCondition);
  thread_mutex_destroy(g_mutex);
}

void executor_run(Job* job) {
  diag_assert_msg(g_jobsIsWorker, "Only job-workers can run jobs");
  (void)job;
}
