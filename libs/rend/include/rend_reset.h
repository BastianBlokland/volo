#pragma once
#include "ecs_module.h"

/**
 * Request the renderer to be re-intialized.
 * Usefull for applying settings that cannot be changed while the renderer is running.
 */
void rend_reset(EcsWorld*);
