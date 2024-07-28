#pragma once

/**
 * Initialize the data system.
 * Should be called once at application startup.
 * Pre-condition: Should only be called from the main-thread.
 */
void data_init(void);

/**
 * Teardown the data subsystem.
 * Should be called once at application shutdown.
 * Pre-condition: Should only be called from the main-thread.
 */
void data_teardown(void);
