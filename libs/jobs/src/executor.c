#include "core_annotation.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_rng.h"
#include "core_thread.h"
#include "jobs_graph.h"
#include "trace_tracer.h"

#include "affinity_queue_internal.h"
#include "executor_internal.h"
#include "graph_internal.h"
#include "scheduler_internal.h"
#include "work_queue_internal.h"

// Amounts of cores reserved for OS and other applications on the system.
// Note: the main-thread is also a worker, so worker count of 1 won't start any additional threads.
#define worker_reserved_core_count 1
#define worker_min_count 1
#define worker_max_count 4

// Maximum amount of tasks that can depend on a single task.
#define job_max_task_children 512

typedef enum {
  ExecMode_Running,
  ExecMode_Teardown,
} ExecMode;

static ExecMode        g_mode = ExecMode_Running; // Change only while holding 'g_mutex'.
static ThreadHandle    g_workerThreads[worker_max_count];
static WorkQueue       g_workerQueues[worker_max_count];
static i32             g_sleepingWorkers;
static ThreadMutex     g_mutex;
static ThreadCondition g_wakeCondition;

/**
 * The affinity queue is a special work-queue for tasks that always need to be executed on the same
 * thread. All threads are allowed to push new work into the queue but only the 'g_affinityWorker'
 * is allowed to pop (and execute) items from it.
 *
 * NOTE: Work in the affinity-queue takes priority over work in the normal work-queue because other
 * threads cannot help out and thus all threads could be waiting for this work to finnish.
 */
static JobWorkerId g_affinityWorker;
static AffQueue    g_affinityQueue;

u16                      g_jobsWorkerCount;
THREAD_LOCAL JobWorkerId g_jobsWorkerId;
THREAD_LOCAL bool        g_jobsIsWorker;
THREAD_LOCAL bool        g_jobsIsWorking;

static void executor_wake_workers(void) {
  thread_mutex_lock(g_mutex);
  thread_cond_broadcast(g_wakeCondition);
  thread_mutex_unlock(g_mutex);
}

static void executor_work_push(Job* job, const JobTaskId task) {
  JobTask* jobTaskDef = dynarray_at_t(&job->graph->tasks, task, JobTask);
  if (UNLIKELY(jobTaskDef->flags & JobTaskFlags_ThreadAffinity)) {
    // Task requires to be run on the affinity worker; push it to the affinity queue.
    affqueue_push(&g_affinityQueue, job, task);
  } else {
    // Task can run on any thread; push it into our own queue.
    workqueue_push(&g_workerQueues[g_jobsWorkerId], job, task);
  }
}

static WorkItem executor_work_pop(void) {
  if (UNLIKELY(g_jobsWorkerId == g_affinityWorker)) {
    /**
     * This worker is the assigned 'Affinity worker' and thus we need to serve the affinity-queue
     * first before taking from our normal queue.
     */
    const WorkItem affinityItem = affqueue_pop(&g_affinityQueue);
    if (UNLIKELY(workitem_valid(affinityItem))) {
      return affinityItem;
    }
  }
  return workqueue_pop(&g_workerQueues[g_jobsWorkerId]);
}

static WorkItem executor_work_steal(void) {
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
    const WorkItem stolenItem = workqueue_steal(&g_workerQueues[victim]);
    if (workitem_valid(stolenItem)) {
      return stolenItem;
    }
  }
  // No work found on any queue.
  return (WorkItem){0};
}

static WorkItem executor_work_affinity_or_steal(void) {
  /**
   * The 'Affinity Worker' is special as it can also receive work from other threads, so while
   * looking for work it also needs to check the affinity-queue.
   */
  if (UNLIKELY(g_jobsWorkerId == g_affinityWorker)) {
    const WorkItem affinityItem = affqueue_pop(&g_affinityQueue);
    if (UNLIKELY(workitem_valid(affinityItem))) {
      return affinityItem;
    }
  }

  return executor_work_steal();
}

static WorkItem executor_work_steal_loop(void) {
  /**
   * Every time-slice attempt to steal work from any other worker, starting from a random worker to
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
  static const usize g_maxIterations = 25;
  for (usize itr = 0; itr != g_maxIterations; ++itr) {

    WorkItem stolenItem = executor_work_affinity_or_steal();
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
  trace_begin_msg("job_task", TraceColor_Green, "{}", fmt_text(jobTaskDef->name));
  {
    g_jobsIsWorking = true;
    jobTaskDef->routine(bits_ptr_offset(jobTaskDef, sizeof(JobTask)));
    g_jobsIsWorking = false;
  }
  trace_end();

  /**
   * Update the tasks that are depending on this work.
   *
   * NOTE: Makes a copy of the child task-ids on the stack before updating any child tasks. The
   * reason is that as soon as we notify a child-task it could finish the entire job while we are
   * still in this function. And thus accessing any WorkItem memory is unsafe after notifying a
   * child.
   */

  JobTaskId childTasks[job_max_task_children];
  usize     childCount = 0;
  jobs_graph_for_task_child(item.job->graph, item.task, child) {
    diag_assert_msg(
        childCount < job_max_task_children,
        "Task has too many children (max: {})",
        fmt_int(job_max_task_children));
    childTasks[childCount++] = child.task;
  }

  // Update the tasks that are depending on this work.
  if (childCount) {
    bool taskPushed = false;
    for (usize i = 0; i != childCount; ++i) {
      // Decrement the dependency counter for the child task.
      if (thread_atomic_sub_i64(&item.job->taskData[childTasks[i]].dependencies, 1) == 1) {
        // All dependencies have been met for child task; push it to the task queue.
        executor_work_push(item.job, childTasks[i]);
        taskPushed = true;
      }
    }
    if (taskPushed && thread_atomic_load_i32(&g_sleepingWorkers)) {
      executor_wake_workers();
    }
    return;
  }

  // Task has no children; decrement the job dependency counter.
  if (thread_atomic_sub_i64(&item.job->dependencies, 1) == 1) {
    // All dependencies for the job have been finished; Finish the job.
    jobs_scheduler_finish(item.job);
  }
}

/**
 * Thread routine for a worker.
 */
static void executor_worker_thread(void* data) {
  g_jobsWorkerId = (JobWorkerId)(uptr)data;
  g_jobsIsWorker = true;

  WorkItem work = (WorkItem){0};
  while (LIKELY(g_mode == ExecMode_Running)) {
    // Perform work if we found some on the previous iteration.
    if (workitem_valid(work)) {
      executor_perform_work(work);
    }

    // Attempt get a work item from our own queues.
    work = executor_work_pop();
    if (workitem_valid(work)) {
      continue; // Perform the work on the next iteration.
    }

    // No work on our own queue; attempt to steal some.
    work = executor_work_steal_loop();
    if (workitem_valid(work)) {
      continue; // Perform the work on the next iteration.
    }

    // No work found; go to sleep.
    thread_mutex_lock(g_mutex);
    thread_atomic_add_i32(&g_sleepingWorkers, 1);
    work = executor_work_affinity_or_steal(); // One last attempt before sleeping.
    if (!workitem_valid(work) && LIKELY(g_mode == ExecMode_Running)) {
      // We don't have any work to perform and we are not cancelled; sleep until woken.
      trace_begin("job_sleep", TraceColor_Gray);
      thread_cond_wait(g_wakeCondition, g_mutex);
      trace_end();
    }
    thread_atomic_sub_i32(&g_sleepingWorkers, 1);
    thread_mutex_unlock(g_mutex);
  }
}

void executor_init(void) {
  g_jobsWorkerCount = math_min(
      math_max(worker_min_count, g_thread_core_count - worker_reserved_core_count),
      worker_max_count);
  g_mutex         = thread_mutex_create(g_alloc_heap);
  g_wakeCondition = thread_cond_create(g_alloc_heap);

  for (u16 i = 0; i != g_jobsWorkerCount; ++i) {
    g_workerQueues[i] = workqueue_create(g_alloc_heap);
  }

  /**
   * Elect the 'affinity worker'.
   * Prefer worker 1 because the main-thread could have other duties that prevent the swift
   * execution of affinity tasks and potentially forcing all workers to wait.
   */
  g_affinityWorker = math_min(g_jobsWorkerCount - 1, 1);
  g_affinityQueue  = affqueue_create(g_alloc_heap);

  // Setup worker info for the main-thread.
  g_jobsWorkerId = 0;
  g_jobsIsWorker = true;
  thread_prioritize(ThreadPriority_High); // NOTE: Can fail due to insufficient perms.

  // Start threads for the other workers.
  for (JobWorkerId i = 1; i != g_jobsWorkerCount; ++i) {
    ThreadPriority threadPrio;
    if (i == g_affinityWorker) {
      // The affinity worker gets higher priority as other workers might depend on work that only
      // it can do.
      threadPrio = ThreadPriority_Highest;
    } else {
      threadPrio = ThreadPriority_High;
    }
    void*        threadData = (void*)(uptr)i;
    const String threadName = fmt_write_scratch("volo_exec_{}", fmt_int(i));
    g_workerThreads[i] = thread_start(executor_worker_thread, threadData, threadName, threadPrio);
  }
}

void executor_teardown(void) {
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
    workqueue_destroy(g_alloc_heap, &g_workerQueues[i]);
  }
  affqueue_destroy(g_alloc_heap, &g_affinityQueue);

  thread_cond_destroy(g_wakeCondition);
  thread_mutex_destroy(g_mutex);
}

void executor_run(Job* job) {
  diag_assert_msg(g_jobsIsWorker, "Only job-workers can run jobs");
  diag_assert_msg(g_jobsWorkerCount, "Job system has to be initialized jobs_init() first.");

  const usize rootTaskCount = jobs_graph_task_root_count(job->graph);

  /**
   * Push work items for all root tasks in the job.
   *
   * NOTE: Its important that we don't touch the job memory after pushing the last root-task. Reason
   * is that another executor could actually finish the job while we are still inside this function.
   */

  JobTaskId taskId = 0;
  for (usize pushedRootTaskCount = 0; pushedRootTaskCount != rootTaskCount; ++taskId) {
    if (jobs_graph_task_has_parent(job->graph, taskId)) {
      continue; // Not a root task.
    }
    executor_work_push(job, taskId);
    ++pushedRootTaskCount;
  }

  if (thread_atomic_load_i32(&g_sleepingWorkers)) {
    executor_wake_workers();
  }
}

bool executor_help(void) {
  // Attempt get a work item from our own queues.
  WorkItem work = executor_work_pop();
  if (workitem_valid(work)) {
    executor_perform_work(work);
    return true;
  }

  // Otherwise attempt to steal a work item.
  work = executor_work_steal();
  if (workitem_valid(work)) {
    executor_perform_work(work);
    return true;
  }

  return false;
}
