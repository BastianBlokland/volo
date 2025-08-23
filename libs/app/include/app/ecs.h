#pragma once
#include "app/type.h"
#include "cli/forward.h"
#include "ecs/def.h"

/**
 * Apis to be implemented by ecs applications.
 */

typedef enum {
  AppEcsStatus_Running,
  AppEcsStatus_Finished,
  AppEcsStatus_Failed,
} AppEcsStatus;

/**
 * Configure the command-line application.
 * Use the various 'cli_register_*' apis from the cli_app.h header.
 */
AppType app_ecs_configure(CliApp*);

/**
 * Register Ecs modules, potentially based on the passed command-line options.
 */
void app_ecs_register(EcsDef*, const CliInvocation*);

/**
 * Initialize the Ecs world.
 * Can be used to add inital entities to the world based on the passed command-line options.
 * NOTE: Return true if initialization succeeded.
 */
bool app_ecs_init(EcsWorld*, const CliInvocation*);

/**
 * Query application status.
 * NOTE: Runs outside of the Ecs update loop so any view can be used to observe the state.
 */
AppEcsStatus app_ecs_status(EcsWorld*);

/**
 * Set application state.
 * NOTE: Runs outside of the Ecs update loop so any view can be used to observe the state.
 */
void app_ecs_set_frame(EcsWorld*, u64 frameIdx);
