#pragma once
#include "cli.h"
#include "ecs_def.h"

/**
 * Apis to be implemented by ecs applications.
 */

/**
 * Configure the command-line application.
 * Use the various 'cli_register_*' apis from the cli_app.h header.
 */
void app_ecs_configure(CliApp*);

/**
 * Register Ecs modules, potentially based on the passed command-line options.
 */
void app_ecs_register(EcsDef*, const CliInvocation*);

/**
 * Initialize the Ecs world.
 * Can be used to add inital entities to the world based on the passed command-line options.
 */
void app_ecs_init(EcsWorld*, const CliInvocation*);

/**
 * Check if the application should quit.
 * NOTE: Runs outside of the Ecs update loop so any view can be used to observe the state.
 */
bool app_ecs_should_quit(EcsWorld*);
