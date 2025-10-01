#include "core/alloc.h"
#include "core/diag.h"
#include "core/dynarray.h"
#include "core/thread.h"
#include "core/time.h"
#include "jobs/scheduler.h"
#include "trace/tracer.h"

#include "executor.h"
#include "init.h"
#include "job.h"

typedef Job* JobPtr;

static i64             g_jobIdCounter;
static ThreadMutex     g_jobMutex;
static ThreadCondition g_jobCondition;
static DynArray        g_runningJobs; // Job*[]. Only access while holding 'g_jobMutex'.
static i32             g_sleepingHelpers;

static bool jobs_scheduler_is_finished_locked(const JobId job) {
  dynarray_for_t(&g_runningJobs, JobPtr, jobData) {
    if ((*jobData)->id == job) {
      return false;
    }
  }
  return true;
}

void scheduler_init(void) {
  g_jobIdCounter = 0;
  g_jobMutex     = thread_mutex_create(g_allocHeap);
  g_jobCondition = thread_cond_create(g_allocHeap);
  g_runningJobs  = dynarray_create_t(g_allocHeap, Job*, 32);
}

void scheduler_teardown(void) {
  thread_mutex_destroy(g_jobMutex);
  thread_cond_destroy(g_jobCondition);
  dynarray_destroy(&g_runningJobs);
}

JobId jobs_scheduler_run(JobGraph* graph, Allocator* alloc) {
  diag_assert_msg(jobs_graph_validate(graph), "Given job graph is invalid");
  diag_assert_msg(g_jobsIsWorker, "Only job-workers can run jobs");

  JobId id = (JobId)thread_atomic_add_i64(&g_jobIdCounter, 1);
  if (UNLIKELY(jobs_graph_task_root_count(graph) == 0)) {
    return id; // Job has no roots tasks; nothing to do.
  }

  trace_begin("job_start", TraceColor_White);

  Job* job = job_create(alloc, id, graph);
  thread_mutex_lock(g_jobMutex);
  *dynarray_push_t(&g_runningJobs, Job*) = job;
  thread_mutex_unlock(g_jobMutex);

  // Note: We cannot touch the 'job' memory anymore after 'executor_run' returns, reason is the job
  // could actually finish while we are still inside this function.
  executor_run(job);

  trace_end();

  return id;
}

bool jobs_scheduler_is_finished(const JobId job) {
  bool finished = true;
  thread_mutex_lock(g_jobMutex);
  finished = jobs_scheduler_is_finished_locked(job);
  thread_mutex_unlock(g_jobMutex);
  return finished;
}

void jobs_scheduler_wait(const JobId job) {
  diag_assert_msg(!jobs_is_working(), "Waiting for a job to finish is not allowed inside a task");

  thread_mutex_lock(g_jobMutex);
  while (!jobs_scheduler_is_finished_locked(job)) {
    thread_cond_wait(g_jobCondition, g_jobMutex);
  }
  thread_mutex_unlock(g_jobMutex);
}

void jobs_scheduler_wait_help(const JobId job) {
  diag_assert_msg(g_jobsIsWorker, "Only job-workers can help out");

  static THREAD_LOCAL u32 g_recursionCount;
  ++g_recursionCount;

  enum { MaxYields = 100 };
  for (u32 yieldsRem = MaxYields;;) {
    if (UNLIKELY(g_recursionCount > 1)) {
      /**
       * We've recursed into the job_scheduler, this means there is a job that is starting
       * additional jobs. To avoid recursing too deeply first finish this job before executing other
       * tasks (which might spawn more jobs).
       */
      if (executor_help_job(job)) {
        yieldsRem = MaxYields;
      }
    } else {
      // Execute all currently available tasks.
      while (executor_help()) {
        yieldsRem = MaxYields;
      }
    }

    if (jobs_scheduler_is_finished(job)) {
      goto Done; // The given job is finished.
    }

    // No tasks more available but the job is not finished; yield our time-slice.
    if (LIKELY(yieldsRem--)) {
      thread_yield();
      continue;
    }

    if (jobs_is_working()) {
      /**
       * When nesting jobs (calling jobs_scheduler_wait_help() inside a job task) we should not
       * sleep the thread as doing so could starve the job-system and lead to a deadlock.
       */
      if (g_jobsWorkerCount == 1) {
        /**
         * NOTE: When using a single worker its important to regularly execute work for other jobs
         * as otherwise we could starve the system.
         */
        executor_help();
      }
      yieldsRem = MaxYields;
      continue;
    }

    // No work has been available for a while; sleep the thread.
    thread_mutex_lock(g_jobMutex);
    thread_atomic_add_i32(&g_sleepingHelpers, 1);

    if (!jobs_scheduler_is_finished_locked(job)) {
      trace_begin("job_sleep", TraceColor_Gray);
      thread_cond_wait(g_jobCondition, g_jobMutex);
      trace_end();
    }
    const bool finished = jobs_scheduler_is_finished_locked(job);

    thread_atomic_sub_i32(&g_sleepingHelpers, 1);
    thread_mutex_unlock(g_jobMutex);
    if (finished) {
      goto Done;
    }
    yieldsRem = MaxYields;
  }

Done:
  --g_recursionCount;
}

void jobs_scheduler_wake_helpers(void) {
  if (thread_atomic_load_i32(&g_sleepingHelpers)) {
    thread_mutex_lock(g_jobMutex);
    thread_cond_broadcast(g_jobCondition);
    thread_mutex_unlock(g_jobMutex);
  }
}

/**
 * Internal api to notify the scheduler that a job has finished.
 */
void jobs_scheduler_finish(Job* job) {
  thread_mutex_lock(g_jobMutex);
  {
    // Cleanup job data.
    job_destroy(job);

    // Remove it from 'g_runningJobs'.
    for (usize i = 0; i != g_runningJobs.size; ++i) {
      Job* other = *dynarray_at_t(&g_runningJobs, i, Job*);
      if (other == job) {
        dynarray_remove(&g_runningJobs, i, 1);
        break;
      }
    }
  }
  thread_mutex_unlock(g_jobMutex);
  thread_cond_broadcast(g_jobCondition);
}

usize jobs_scheduler_mem_size(const JobGraph* graph) { return job_mem_req_size(graph); }
usize jobs_scheduler_mem_align(const JobGraph* graph) { return job_mem_req_align(graph); }
