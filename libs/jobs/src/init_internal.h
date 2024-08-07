#pragma once
#include "jobs_init.h"

/**
 * Global initialization routines.
 * Fired once when the job library is initialized.
 */
void scheduler_init(void);
void executor_init(const JobsConfig*);

/**
 * Global teardown routines.
 * Fired once when the job library is torn down.
 */
void scheduler_teardown(void);
void executor_teardown(void);
