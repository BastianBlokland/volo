#pragma once

/**
 * Initialize the logging system.
 * Should be called once at application startup.
 * Pre-condition: Should only be called from the main-thread.
 */
void log_init();

/**
 * Teardown the log subsystem.
 * Should be called once at application shutdown.
 * Pre-condition: Should only be called from the main-thread.
 */
void log_teardown();
