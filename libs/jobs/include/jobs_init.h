#pragma once
#include "core.h"

typedef struct {
  /**
   * Amount of workers.
   * If set higher then one (main-thread) additional threads are spawned to help out.
   * NOTE: When set to zero it will be automatically based on the system cpu core count.
   */
  u16 workerCount;
} JobsConfig;

/**
 * Initialize the jobs system.
 * Should be called once at application startup.
 * Pre-condition: Should only be called from the main-thread.
 */
void jobs_init(const JobsConfig*);

/**
 * Teardown the jobs subsystem.
 * Should be called once at application shutdown.
 * Pre-condition: Should only be called from the main-thread.
 */
void jobs_teardown(void);
