#pragma once
#include "ecs_def.h"

/**
 * Register the ecs modules for the Renderer library.
 */
void rend_register(EcsDef*);

/**
 * Teardown the render library.
 * Call this before application exit (or more precisely before calling 'ecs_world_destroy()').
 *
 * Pre-condition: !ecs_world_busy()
 */
void rend_teardown(EcsWorld*);
