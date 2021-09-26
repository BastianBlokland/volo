#pragma once
#include "ecs_entity.h"

/**
 * Entity index, assigned in a first-free manor. Unique within all alive entities.
 */
#define ecs_entity_id_index(_ENTITY_ID_) ((u32)((_ENTITY_ID_) >> 0))

/**
 * Unique identifier of the entity, assigned linearly starting from 1.
 * Not meant to wrap around as it breaks the invariant that EntityIds are never reused, if a bigger
 * serial counter is needed then more bits can be assigned (the index part most likely doesn't need
 * 32 bits).
 */
#define ecs_entity_id_serial(_ENTITY_ID_) ((u32)((_ENTITY_ID_) >> 32))
