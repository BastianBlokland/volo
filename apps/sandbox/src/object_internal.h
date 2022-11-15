#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"
#include "geo_quat.h"
#include "scene_faction.h"

/**
 * Global object database.
 */
ecs_comp_extern(ObjectDatabaseComp);

/**
 * An individual object.
 */
ecs_comp_extern(ObjectComp);
ecs_comp_extern(ObjectUnitComp);

/**
 * Spawn new objects.
 */
EcsEntityId object_spawn_unit(EcsWorld*, const ObjectDatabaseComp*, GeoVector pos, SceneFaction);
EcsEntityId object_spawn_unit_player(EcsWorld*, const ObjectDatabaseComp*, GeoVector pos);
EcsEntityId object_spawn_unit_ai(EcsWorld*, const ObjectDatabaseComp*, GeoVector pos);
EcsEntityId object_spawn_wall(EcsWorld*, const ObjectDatabaseComp*, GeoVector pos, GeoQuat rot);
