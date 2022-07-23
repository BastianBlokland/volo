#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_vector.h"

/**
 * Global unit database.
 */
ecs_comp_extern(UnitDatabaseComp);

/**
 * An individual unit.
 */
ecs_comp_extern(UnitComp);

/**
 * Spawn a new unit at the given position.
 */
EcsEntityId unit_spawn(EcsWorld*, const UnitDatabaseComp*, GeoVector position);
