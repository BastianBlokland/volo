#pragma once
#include "core/bitset.h"
#include "ecs/def.h"

#include "iterator_internal.h"

/**
 * An Archetype is a container that stores entities with a specific set of components.
 * When an Entity's layout is changed (a component added or removed) it is moved to a different
 * archetype.
 */

// 64 bytes to fit in a single cacheline on x86.
#define ecs_archetype_size 64

typedef struct {
  ALIGNAS(ecs_archetype_size) BitSet mask;
  u32    entitiesPerChunk;
  u32    compCount;
  u16*   compOffsetsAndSizes; // u16 offsets[compCount], u16 sizes[compCount].
  void** chunks;              // void* chunks[chunkCount].
  u32    chunkCount;
  u32    entityCount;
} EcsArchetype;

ASSERT(sizeof(EcsArchetype) == ecs_archetype_size, "Invalid archetype size");

EcsArchetype ecs_archetype_create(const EcsDef*, BitSet mask);
void         ecs_archetype_destroy(EcsArchetype*);
u32          ecs_archetype_chunks_non_empty(const EcsArchetype*);
usize        ecs_archetype_total_size(const EcsArchetype*);
u32          ecs_archetype_add(EcsArchetype*, EcsEntityId);
EcsEntityId  ecs_archetype_remove(EcsArchetype*, u32 index);

bool ecs_archetype_itr_walk(EcsArchetype*, EcsIterator*);
void ecs_archetype_itr_jump(EcsArchetype*, EcsIterator*, u32 index);

void ecs_archetype_copy_across(
    BitSet mask, EcsArchetype* dst, u32 dstIdx, EcsArchetype* src, u32 srcIdx);
