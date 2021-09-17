#include "core_diag.h"
#include "core_math.h"
#include "core_rng.h"
#include "core_thread.h"

#include "executor_internal.h"
#include "graph_internal.h"
#include "jobs_graph.h"
#include "scheduler_internal.h"
#include "work_queue_internal.h"

// Amounts of cores reserved for OS and other applications on the system.
#define worker_reserved_core_count 1
#define worker_min_count 1
#define worker_max_count 64

typedef enum {
  ExecMode_Running,
  ExecMode_Teardown,
} ExecMode;

static ExecMode        g_mode = ExecMode_Running; // Change only while holding 'g_mutex'.
static ThreadHandle    g_workerThreads[worker_max_count];
static WorkQueue       g_workerQueues[worker_max_count];
static i64             g_sleepingWorkers;
static ThreadMutex     g_mutex;
static ThreadCondition g_wakeCondition;

u16                      g_jobsWorkerCount;
THREAD_LOCAL JobWorkerId g_jobsWorkerId;
THREAD_LOCAL bool        g_jobsIsWorker;
THREAD_LOCAL bool        g_jobsIsWorking;

static WorkItem executor_steal() {
  /**
   * Attempt to steal work from any other worker, starting from a random worker to reduce
   * contention.
   */
  JobWorkerId prefVictim = (JobWorkerId)rng_sample_range(g_rng, 0, g_jobsWorkerCount);
  for (u16 i = 0; i != g_jobsWorkerCount; ++i) {
    JobWorkerId victim = (prefVictim + i) % g_jobsWorkerCount;
    if (victim == g_jobsWorkerId) {
      continue; // Don't steal from ourselves.
    }
    WorkItem stolenItem = workqueue_steal(&g_workerQueues[victim]);
    if (workitem_valid(stolenItem)) {
      return stolenItem;
    }
  }
  // No work found on any queue.
  return (WorkItem){0};
}

static WorkItem executor_steal_loop() {
  /**
   * Every time-slice attempt to steal work any other worker, starting from a random worker to
   * reduce contention.
   *
   * There is allot of experimentation that could be done here:
   * - Keeping track of the last victim we successfully stole from could reduce the amount of
   *   iterations needed.
   * - Spinning (perhaps using pause cpu intrinsics) instead of yielding (or some combo of both) to
   *   reduce the context switching when there isn't any work for a very brief moment.
   * - Fully random picking of workers to steal from (instead of linear with random offset) could
   *   reduce contention.
   */
  static const usize maxIterations = 1000;
  for (usize itr = 0; itr != maxIterations; ++itr) {

    WorkItem stolenItem = executor_steal();
    if (workitem_valid(stolenItem)) {
      return stolenItem;
    }

    // No work found this iteration; yield our timeslice.
    thread_yield();
  }
  // No work found after 'maxIterations' timeslices; time to go to sleep.
  return (WorkItem){0};
}

static void executor_perform_work(WorkItem item) {
  // Get the JobTask definition from the graph.
  JobTask* jobTaskDef = dynarray_at_t(&item.job->graph->tasks, item.task, JobTask);

  // Invoke the user routine.
  g_jobsIsWorking = true;
  jobTaskDef->routine((u8*)jobTaskDef + sizeof(JobTask));
  g_jobsIsWorking = false;

  // Update the tasks that are depending on this work.
  if (jobs_graph_task_has_child(item.job->graph, item.task)) {
    bool taskPushed = false;
    jobs_graph_for_task_child(item.job->graph, item.task, child, {
      // Decrement the dependency counter for the child task.
      if (thread_atomic_sub_i64(&item.job->taskData[child.task].dependencies, 1) == 1) {
        // All dependencies have been met for child task; push it to the task queue.
        workqueue_push(&g_workerQueues[g_jobsWorkerId], item.job, child.task);
        taskPushed = true;
      }
    });
    if (taskPushed && g_sleepingWorkers) {
      // Some workers are sleeping: Wake them.
      thread_cond_broadcast(g_wakeCondition);
    }
    return;
  }

  // Task has no children; decrement the job depedency counter.
  if (thread_atomic_sub_i64(&item.job->dependencies, 1) == 1) {
    // All dependencies for the job have been finished; Finish the job.
    jobs_scheduler_finish(item.job);
  }
}

/**
 * Thread routine for a worker.
 */
static void executor_worker_thread(void* data) {
  g_jobsWorkerId = (JobWorkerId)(usize)data;
  g_jobsIsWorker = true;

  WorkItem work = (WorkItem){0};
  while (LIKELY(g_mode == ExecMode_Running)) {
    // Perform work if we found some on the previous iteration.
    if (workitem_valid(work)) {
      executor_perform_work(work);
    }

    // Attempt get a work item from our own queue.
    work = workqueue_pop(&g_workerQueues[g_jobsWorkerId]);
    if (!workitem_valid(work)) {
      // No work on our own queue; attemp to steal some.
      work = executor_steal_loop();
    }

    if (workitem_valid(work)) {
      continue; // Perform the work on the next iteration.
    }

    // No work found; go to sleep.
    thread_mutex_lock(g_mutex);
    work = executor_steal(); // One last attempt to find work while holding the mutex.
    if (!workitem_valid(work) && LIKELY(g_mode == ExecMode_Running)) {
      // We don't have any work to perform and we are not cancelled; sleep until woken.
      ++g_sleepingWorkers; // No atomic operation as we are holding the lock atm.
      thread_cond_wait(g_wakeCondition, g_mutex);
      --g_sleepingWorkers; // No atomic operation as we are holding the lock atm.
    }
    thread_mutex_unlock(g_mutex);
  }
}

void executor_init() {
  g_jobsWorkerCount = math_min(
      math_max(worker_min_count, g_thread_core_count - worker_reserved_core_count),
      worker_max_count);
  g_mutex         = thread_mutex_create(g_alloc_heap);
  g_wakeCondition = thread_cond_create(g_alloc_heap);

  for (u16 i = 0; i != g_jobsWorkerCount; ++i) {
    g_workerQueues[i] = workqueue_create(g_alloc_heap);
  }

  // Setup worker info for the main-thread.
  g_jobsWorkerId = 0;
  g_jobsIsWorker = true;

  // Start threads for the other workers.
  for (u16 i = 1; i != g_jobsWorkerCount; ++i) {
    g_workerThreads[i] = thread_start(
        executor_worker_thread, (void*)(usize)i, fmt_write_scratch("jobs_exec_{}", fmt_int(i)));
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
    if (workqueue_size(&g_workerQueues[i])) {
      diag_print_err(
          "jobs_executor: Worker {} has {} unfinished tasks.\n",
          fmt_int(i),
          fmt_int(workqueue_size(&g_workerQueues[i])));
    }
    workqueue_destroy(g_alloc_heap, &g_workerQueues[i]);
  }

  thread_cond_destroy(g_wakeCondition);
  thread_mutex_destroy(g_mutex);
}

void executor_run(Job* job) {
  diag_assert_msg(g_jobsIsWorker, "Only job-workers can run jobs");
  diag_assert_msg(g_jobsWorkerCount, "Job system has to be initialized jobs_init() first.");

  // Push work items for all root tasks in the job.
  jobs_graph_for_task(job->graph, taskId, {
    if (jobs_graph_task_has_parent(job->graph, taskId)) {
      continue; // Not a root task.
    }
    workqueue_push(&g_workerQueues[g_jobsWorkerId], job, taskId);
  });

  if (g_sleepingWorkers) {
    // Some workers are sleeping: Wake them.
    thread_cond_broadcast(g_wakeCondition);
  }
}

bool executor_help() {
  // Attempt get a work item from our own queue.
  WorkItem work = workqueue_pop(&g_workerQueues[g_jobsWorkerId]);
  if (workitem_valid(work)) {
    executor_perform_work(work);
    return true;
  }

  // Otherwise attempt to steal a work item.
  work = executor_steal();
  if (workitem_valid(work)) {
    executor_perform_work(work);
    return true;
  }

  return false;
}
