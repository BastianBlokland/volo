#pragma once

/**
 * Initialize all the network subsystems.
 * Should be called once at application startup.
 */
void net_init(void);

/**
 * Teardown all the network subsystems.
 * Should be called once at application shutdown.
 */
void net_teardown(void);
