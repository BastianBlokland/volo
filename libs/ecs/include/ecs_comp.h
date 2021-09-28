#pragma once
#include "core_types.h"

/**
 * Identifier for a component type.
 */
typedef u16 EcsCompId;

/**
 * Compare two EcsCompId's.
 * Signature is compatible with the 'CompareFunc' from 'core_compare.h'.
 */
i8 ecs_compare_comp(const void* a, const void* b);
