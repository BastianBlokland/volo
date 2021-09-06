#pragma once

/**
 * Initialize the jobs system.
 * Should be called once at application startup.
 * Pre-condition: Should only be called from the main-thread.
 */
void jobs_init();

/**
 * Teardown the jobs subsystem.
 * Should be called once at application shutdown.
 * Pre-condition: Should only be called from the main-thread.
 */
void jobs_teardown();
