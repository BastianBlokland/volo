#pragma once
#include "core_bitset.h"
#include "core_dynarray.h"
#include "ecs_def.h"

#include "entity_internal.h"

/**
 * An Archetype is a container that stores entities with a specific set of components.
 * When an Entity's layout is changed (a component added or removed) it is moved to a different
 * archtype.
 */

// 64 bytes to fit in a single cacheline on x86.
#define ecs_archetype_size 64

typedef struct {
  ALIGNAS(ecs_archetype_size) BitSet mask;
  usize  entitiesPerChunk;
  u16*   compOffsetsAndStrides; // u16 offsets[compCount], u16 strides[compCount].
  usize  compCount;
  void** chunks; // void* chunks[chunkCount].
  usize  chunkCount;
  usize  entityCount;
} EcsArchetype;

ASSERT(sizeof(EcsArchetype) == ecs_archetype_size, "Invalid archetype size");

EcsArchetype ecs_archetype_create(const EcsDef*, BitSet mask);
void         ecs_archetype_destroy(EcsArchetype*);
u32          ecs_archetype_add(EcsArchetype*, EcsEntityId);
EcsEntityId  ecs_archetype_remove(EcsArchetype*, u32 index);
EcsEntityId* ecs_archetype_entity(EcsArchetype*, u32 index);
void*        ecs_archetype_comp(EcsArchetype*, u32 index, EcsCompId);
