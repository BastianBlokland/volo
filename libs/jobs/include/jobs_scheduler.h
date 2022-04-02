#pragma once
#include "jobs_graph.h"

/**
 * Handle to a started job (which may or may not still be running).
 * Unique throughout the application lifetime.
 */
typedef u64 JobId;

/**
 * Run a new job graph the given graph definition.
 * Returns a handle to the running job.
 * Pre-condition: g_jobsIsWorker == true
 */
JobId jobs_scheduler_run(JobGraph* graph, Allocator*);

/**
 * Check if the given job has finished yet.
 */
bool jobs_scheduler_is_finished(JobId);

/**
 * Wait for the given job to finished.
 * NOTE: Calling thread will be put to sleep until the job has finished.
 * Pre-condition: g_jobsIsWorking == false
 */
void jobs_scheduler_wait(JobId);

/**
 * Help with executing tasks until the given job is finished.
 * NOTE: Calling thread will help out with any queued tasks, not only tasks for the given job.
 * Pre-condition: g_jobsIsWorker == true
 * Pre-condition: g_jobsIsWorking == false
 */
void jobs_scheduler_wait_help(JobId);

/**
 * Query the required memory size and alignment to run the given job graph.
 */
usize jobs_scheduler_mem_size(const JobGraph*);
usize jobs_scheduler_mem_align(const JobGraph*);
