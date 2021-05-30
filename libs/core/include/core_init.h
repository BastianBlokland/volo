#pragma once

/**
 * Initialize all the core subsystems.
 * Should be called once at application startup.
 */
void core_init();

/**
 * Teardown all the core subsystems.
 * Should be called once at application shutdown.
 */
void core_teardown();
