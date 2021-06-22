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
JobId jobs_scheduler_run(JobGraph* graph);

/**
 * Check if the given job has finished yet.
 */
bool jobs_scheduler_is_finished(JobId);

/**
 * Wait for the given job to finished.
 * Note: Calling thread will be put to sleep until the job has finished.
 */
void jobs_scheduler_wait(JobId);
