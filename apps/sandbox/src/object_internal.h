#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_vector.h"

/**
 * Global object database.
 */
ecs_comp_extern(ObjectDatabaseComp);

/**
 * An individual object.
 */
ecs_comp_extern(ObjectComp);

/**
 * Spawn new objects.
 */
EcsEntityId object_spawn_unit(EcsWorld*, const ObjectDatabaseComp*, GeoVector position);
EcsEntityId object_spawn_wall(EcsWorld*, const ObjectDatabaseComp*, GeoVector position);
