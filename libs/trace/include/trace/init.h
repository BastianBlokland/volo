#pragma once

/**
 * Initialize the tracing system.
 * Should be called once at application startup.
 * Pre-condition: Should only be called from the main-thread.
 */
void trace_init(void);

/**
 * Teardown the tracing subsystem.
 * Should be called once at application shutdown.
 * Pre-condition: Should only be called from the main-thread.
 */
void trace_teardown(void);
