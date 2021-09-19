#pragma once
#include "core_bitset.h"
#include "core_dynarray.h"
#include "ecs_storage.h"

typedef struct {
  BitSet mask;
  usize  entitiesPerChunk;
  u16*   compOffsetsAndStrides; // u16 offsets[compCount], u16 strides[compCount].
  usize  compCount;
  void** chunks; // void* chunks[chunkCount].
  usize  chunkCount;
  usize  entityCount;
} EcsArchetype;

EcsArchetype ecs_archetype_create(EcsDef*, BitSet mask);
void         ecs_archetype_destroy(EcsArchetype*);
u32          ecs_archetype_add(EcsArchetype*, EcsEntityId);
EcsEntityId* ecs_archetype_get_entity(EcsArchetype*, u32 index);
void*        ecs_archetype_get_comp(EcsArchetype*, u32 index, EcsCompId);
