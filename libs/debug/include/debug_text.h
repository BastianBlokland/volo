#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_vector.h"

ecs_comp_extern(DebugTextComp);

/**
 * Add a new debug-text component to the given entity.
 */
DebugTextComp* debug_text_create(EcsWorld*, EcsEntityId entity);

/**
 * Draw primitives.
 */
void debug_text(DebugTextComp*, GeoVector pos, String);
