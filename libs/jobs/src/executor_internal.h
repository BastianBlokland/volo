#include "job.h"
#include "jobs_executor.h"

void executor_run(Job*);

/**
 * Help with executing tasks.
 * Returns true if we executed any work or false if there was no work to execute.
 */
bool executor_help();
