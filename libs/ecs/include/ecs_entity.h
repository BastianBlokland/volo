#pragma once
#include "core_types.h"

/**
 * Identifier for an Entity, unique throughout application lifetime.
 */
typedef u64 EcsEntityId;

/**
 * Entity index, assigned in a first-free manner. Unique within all currently existing entities.
 */
#define ecs_entity_id_index(_ENTITY_ID_) ((u32)((_ENTITY_ID_) >> 0))

/**
 * Unique identifier of the entity, assigned linearly starting from 1.
 * Not meant to wrap around as it breaks the invariant that EntityIds are never reused, if a bigger
 * serial counter is needed then more bits can be assigned (the index part most likely doesn't need
 * 32 bits).
 */
#define ecs_entity_id_serial(_ENTITY_ID_) ((u32)((_ENTITY_ID_) >> 32))

/**
 * Check if the given entity-id is valid.
 * NOTE: Does not mean that the entity actually exists in a world.
 */
#define ecs_entity_valid(_ENTITY_ID_) (ecs_entity_id_serial(_ENTITY_ID_) != 0)

/**
 * Compare two EcsEntityId's.
 * Signature is compatible with the 'CompareFunc' from 'core_compare.h'.
 */
i8 ecs_compare_entity(const void* a, const void* b);
