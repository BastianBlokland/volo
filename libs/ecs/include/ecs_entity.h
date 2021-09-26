#pragma once
#include "core_types.h"

/**
 * Identifier for an Entity, unique throughout application lifetime.
 */
typedef u64 EcsEntityId;

/**
 * Compare two EcsEntityId's.
 * Signature is compatible with the 'CompareFunc' from 'core_compare.h'.
 */
i8 ecs_compare_entity(const void* a, const void* b);
