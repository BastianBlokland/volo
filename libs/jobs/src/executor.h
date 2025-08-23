#include "jobs/executor.h"

#include "job.h"

void executor_run(Job*);

/**
 * Help with executing tasks.
 * Returns true if we executed any work or false if there was no work to execute.
 */
bool executor_help(void);
bool executor_help_job(JobId);
