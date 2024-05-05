#include "core_array.h"
#include "core_math.h"
#include "core_rng.h"
#include "core_thread.h"
#include "jobs_graph.h"
#include "jobs_init.h"
#include "trace_tracer.h"

#include "affinity_queue_internal.h"
#include "executor_internal.h"
#include "graph_internal.h"
#include "scheduler_internal.h"
#include "work_queue_internal.h"

#include <immintrin.h>

// Note: the main-thread is also a worker, so worker count of 1 won't start any additional threads.
#define worker_min_count 1
#define worker_max_count 4

// Maximum amount of root tasks in a job.
#define job_max_root_tasks 1024

// Maximum amount of tasks that can depend on a single task.
#define job_max_task_children 128

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
THREAD_LOCAL JobTaskId   g_jobsTaskId;
THREAD_LOCAL Job* g_jobsCurrent;

static void executor_wake_worker_all(void) {
  thread_mutex_lock(g_mutex);
  thread_cond_broadcast(g_wakeCondition);
  thread_mutex_unlock(g_mutex);
}

static void executor_wake_worker_single(void) {
  thread_mutex_lock(g_mutex);
  thread_cond_signal(g_wakeCondition);
  thread_mutex_unlock(g_mutex);
}

static WorkItem executor_work_pop(const JobWorkerId wId) {
  if (wId == g_affinityWorker) {
    /**
     * This worker is the assigned 'Affinity worker' and thus we need to serve the affinity-queue
     * first before taking from our normal queue.
     */
    const WorkItem affinityItem = affqueue_pop(&g_affinityQueue);
    if (workitem_valid(affinityItem)) {
      return affinityItem;
    }
  }
  return workqueue_pop(&g_workerQueues[wId]);
}

static WorkItem executor_work_steal(const JobWorkerId wId) {
  /**
   * Attempt to steal work from any other worker, starting from a random worker to reduce
   * contention.
   */
  JobWorkerId prefVictim = (JobWorkerId)rng_sample_range(g_rng, 0, g_jobsWorkerCount);
  for (u16 i = 0; i != g_jobsWorkerCount; ++i) {
    JobWorkerId victim = (prefVictim + i) % g_jobsWorkerCount;
    if (victim == wId) {
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

static WorkItem executor_work_affinity_or_steal(const JobWorkerId wId) {
  /**
   * The 'Affinity Worker' is special as it can also receive work from other threads, so while
   * looking for work it also needs to check the affinity-queue.
   */
  if (wId == g_affinityWorker) {
    const WorkItem affinityItem = affqueue_pop(&g_affinityQueue);
    if (workitem_valid(affinityItem)) {
      return affinityItem;
    }
  }

  return executor_work_steal(wId);
}

static WorkItem executor_work_steal_loop(const JobWorkerId wId) {
  /**
   * Attempt to steal work from any other worker, try for some iterations before giving up.
   */
  static const usize g_maxIterations = 2500;
  for (usize itr = 0; itr != g_maxIterations; ++itr) {

    WorkItem stolenItem = executor_work_affinity_or_steal(wId);
    if (workitem_valid(stolenItem)) {
      return stolenItem;
    }

    // No work found this iteration; spin or yield our timeslice.
    if (itr % 100) {
      _mm_pause();
    } else {
      thread_yield();
    }
  }
  // No work found; time to go to sleep.
  return (WorkItem){0};
}

static void executor_perform_work(const JobWorkerId wId, const WorkItem item) {
  // Get the JobTask definition from the graph.
  const JobTask* jobTaskDef = jobs_graph_task_def(item.job->graph, item.task);

  // Invoke the user routine.
  trace_begin_msg("job_task", TraceColor_Green, "{}", fmt_text(jobTaskDef->name));
  {
    const void* userCtx = bits_ptr_offset(jobTaskDef, sizeof(JobTask));
    g_jobsTaskId        = item.task;
    g_jobsCurrent       = item.job;
    jobTaskDef->routine(userCtx);
    g_jobsCurrent = null;
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
  u32       childCount = 0;
  jobs_graph_for_task_child(item.job->graph, item.task, child) {
    diag_assert_msg(
        childCount < job_max_task_children,
        "Task has too many children (max: {})",
        fmt_int(job_max_task_children));
    childTasks[childCount++] = child.task;
  }

  // Update the tasks that are depending on this work.
  if (childCount) {
    u32 tasksPushed = 0, tasksPushedAffinity = 0;
    for (u32 i = 0; i != childCount; ++i) {
      // Decrement the dependency counter for the child task.
      if (thread_atomic_sub_i64(&item.job->taskData[childTasks[i]].dependencies, 1) == 1) {
        // All dependencies have been met for child task; push it to the task queue.
        const JobTask* childTaskDef = jobs_graph_task_def(item.job->graph, childTasks[i]);
        if (childTaskDef->flags & JobTaskFlags_ThreadAffinity) {
          affqueue_push(&g_affinityQueue, item.job, childTasks[i]);
          ++tasksPushedAffinity;
        } else {
          workqueue_push(&g_workerQueues[wId], item.job, childTasks[i]);
        }
        ++tasksPushed;
      }
    }
    const bool requireAffinityWorker = tasksPushedAffinity && wId != g_affinityWorker;
    const bool needHelp              = tasksPushed > 1 || requireAffinityWorker;
    if (needHelp && thread_atomic_load_i32(&g_sleepingWorkers)) {
      if (tasksPushed > 2 || requireAffinityWorker) {
        executor_wake_worker_all();
      } else {
        executor_wake_worker_single();
      }
    }
  } else {
    // Task has no children; decrement the job dependency counter.
    if (thread_atomic_sub_i64(&item.job->dependencies, 1) == 1) {
      // All dependencies for the job have been finished; Finish the job.
      jobs_scheduler_finish(item.job);
    }
  }
}

/**
 * Thread routine for a worker.
 */
static void executor_worker_thread(void* data) {
  const JobWorkerId wId = (JobWorkerId)(uptr)data;

  // Setup thread-local data.
  // NOTE: Prefer accessing the variables on the stack to avoid the thread-local indirections.
  g_jobsWorkerId = wId;
  g_jobsIsWorker = true;

  WorkItem work = (WorkItem){0};
  while (LIKELY(g_mode == ExecMode_Running)) {
    // Perform work if we found some on the previous iteration.
    if (workitem_valid(work)) {
      executor_perform_work(wId, work);
    }

    // Attempt get a work item from our own queues.
    work = executor_work_pop(wId);
    if (workitem_valid(work)) {
      continue; // Perform the work on the next iteration.
    }

    // No work on our own queue; attempt to steal some.
    work = executor_work_steal_loop(wId);
    if (workitem_valid(work)) {
      continue; // Perform the work on the next iteration.
    }

    // No work found; go to sleep.
    thread_mutex_lock(g_mutex);
    thread_atomic_add_i32(&g_sleepingWorkers, 1);
    work = executor_work_affinity_or_steal(wId); // One last attempt before sleeping.
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

static u16 executor_worker_count_desired(const JobsConfig* cfg) {
  if (cfg->workerCount) {
    return cfg->workerCount;
  }
  // Amounts of cores reserved for OS and other applications on the system.
  const u16 reservedCoreCount = 1;
  return g_thread_core_count - reservedCoreCount;
}

static u16 executor_worker_count(const JobsConfig* cfg) {
  const u16 desiredCount = executor_worker_count_desired(cfg);
  return math_min(math_max(desiredCount, worker_min_count), worker_max_count);
}

void executor_init(const JobsConfig* cfg) {
  g_jobsWorkerCount = executor_worker_count(cfg);
  g_mutex           = thread_mutex_create(g_alloc_heap);
  g_wakeCondition   = thread_cond_create(g_alloc_heap);

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

  /**
   * Collect all the root tasks in the job.
   *
   * NOTE: Makes a copy of the task-ids on the stack before starting the tasks. The reason is that
   * as soon as we start the last root-task it can actually finish the entire job while we are still
   * in this function. And thus accessing the job memory is unsafe after starting the last task.
   */
  JobTaskId tasksNormal[job_max_root_tasks];
  JobTaskId tasksAffinity[job_max_root_tasks];
  u32       tasksNormalCount = 0, tasksAffinityCount = 0;

  jobs_graph_for_task(job->graph, task) {
    if (jobs_graph_task_has_parent(job->graph, task)) {
      continue; // Not a root task.
    }

    diag_assert_msg(
        tasksNormalCount < job_max_root_tasks && tasksAffinityCount < job_max_root_tasks,
        "Job has too root tasks (max: {})",
        fmt_int(job_max_root_tasks));

    const JobTask* taskDef = jobs_graph_task_def(job->graph, task);
    if (taskDef->flags & JobTaskFlags_ThreadAffinity) {
      tasksAffinity[tasksAffinityCount++] = task;
    } else {
      tasksNormal[tasksNormalCount++] = task;
    }
  }

  // Start all affinity root tasks.
  for (u32 i = 0; i != tasksAffinityCount; ++i) {
    affqueue_push(&g_affinityQueue, job, tasksAffinity[i]);
  }

  // Start all normal root tasks.
  const JobWorkerId wId = g_jobsWorkerId;
  for (u32 i = 0; i != tasksNormalCount; ++i) {
    workqueue_push(&g_workerQueues[wId], job, tasksNormal[i]);
  }

  if (thread_atomic_load_i32(&g_sleepingWorkers)) {
    executor_wake_worker_all();
  }
}

bool executor_help(void) {
  const JobWorkerId wId = g_jobsWorkerId;

  // Attempt get a work item from our own queues.
  WorkItem work = executor_work_pop(wId);
  if (workitem_valid(work)) {
    executor_perform_work(wId, work);
    return true;
  }

  // Otherwise attempt to steal a work item.
  work = executor_work_steal(wId);
  if (workitem_valid(work)) {
    executor_perform_work(wId, work);
    return true;
  }

  return false;
}

bool jobs_is_working(void) { return g_jobsCurrent != null; }

Mem jobs_scratchpad(const JobTaskId task) {
  diag_assert_msg(g_jobsCurrent, "No active job");
  diag_assert(task < jobs_graph_task_count(g_jobsCurrent->graph));
  return array_mem(g_jobsCurrent->taskData[task].scratchpad);
}
