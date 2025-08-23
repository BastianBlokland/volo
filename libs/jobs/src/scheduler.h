#include "jobs/scheduler.h"

#include "job.h"

/**
 * Wake any sleeping helpers.
 */
void jobs_scheduler_wake_helpers(void);

/**
 * Internal api to notify the scheduler that a job has finished.
 */
void jobs_scheduler_finish(Job*);
