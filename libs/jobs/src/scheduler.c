#include "core_alloc.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_thread.h"
#include "jobs_scheduler.h"

#include "executor_internal.h"
#include "init_internal.h"
#include "job_internal.h"

static i64             g_jobIdCounter;
static ThreadMutex     g_jobMutex;
static ThreadCondition g_jobCondition;
static DynArray        g_runningJobs; // Job*[]. Only access while holding 'g_jobMutex'.

static bool jobs_scheduler_is_finished_locked(const JobId job) {
  dynarray_for_t(&g_runningJobs, Job*, jobData, {
    if ((*jobData)->id == job) {
      return false;
    }
  });
  return true;
}

void scheduler_init() {
  g_jobIdCounter = 0;
  g_jobMutex     = thread_mutex_create(g_alloc_heap);
  g_jobCondition = thread_cond_create(g_alloc_heap);
  g_runningJobs  = dynarray_create_t(g_alloc_heap, Job*, 32);
}

void scheduler_teardown() {
  thread_mutex_destroy(g_jobMutex);
  thread_cond_destroy(g_jobCondition);
  dynarray_destroy(&g_runningJobs);
}

JobId jobs_scheduler_run(JobGraph* graph) {
  diag_assert_msg(jobs_graph_validate(graph), "Given job graph is invalid");
  diag_assert_msg(g_jobsIsWorker, "Only job-workers can run jobs");

  JobId id = (JobId)thread_atomic_add_i64(&g_jobIdCounter, 1);
  if (UNLIKELY(jobs_graph_task_root_count(graph) == 0)) {
    return id; // Job has no roots tasks; nothing to do.
  }

  Job* job = job_create(g_alloc_heap, id, graph);
  thread_mutex_lock(g_jobMutex);
  *dynarray_push_t(&g_runningJobs, Job*) = job;
  thread_mutex_unlock(g_jobMutex);

  // Note: We cannot touch the 'job' memory anymore after 'executor_run' returns, reason is the job
  // could actually finish while we are still inside this function.
  executor_run(job);

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
  diag_assert_msg(!g_jobsIsWorking, "Waiting for a job to finish is not allowed inside a task");

  thread_mutex_lock(g_jobMutex);
  while (!jobs_scheduler_is_finished_locked(job)) {
    thread_cond_wait(g_jobCondition, g_jobMutex);
  }
  thread_mutex_unlock(g_jobMutex);
}

void jobs_scheduler_wait_help(const JobId job) {
  diag_assert_msg(g_jobsIsWorker, "Only job-workers can help out");

  while (true) {
    // Execute all currently available tasks.
    while (executor_help())
      ;

    if (jobs_scheduler_is_finished(job)) {
      return; // The given job is finished.
    }

    // No tasks more available but the job is not finished; yield our time-slice.
    thread_yield();
  }
}

/**
 * Internal api to notify the scheduler that a job has finished.
 */
void jobs_scheduler_finish(Job* job) {
  thread_mutex_lock(g_jobMutex);
  {
    // Cleanup job data.
    job_destroy(g_alloc_heap, job);

    // Remove it from 'g_runningJobs'.
    dynarray_for_t(&g_runningJobs, Job*, other, {
      if (*other == job) {
        dynarray_remove(&g_runningJobs, other_i, 1);
        break;
      }
    });
  }
  thread_mutex_unlock(g_jobMutex);
  thread_cond_broadcast(g_jobCondition);
}
