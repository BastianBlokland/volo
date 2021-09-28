#pragma once
#include "core_bitset.h"
#include "core_dynarray.h"
#include "ecs_def.h"

#include "entity_internal.h"

/**
 * Buffer for storing entity layout modifications to be applied at a later time.
 */

typedef enum {
  EcsBufferEntityFlags_None    = 0,
  EcsBufferEntityFlags_Destroy = 1 << 0,
} EcsBufferEntityFlags;

typedef struct {
  const EcsDef* def;
  DynArray      masks;    // u8[bits_to_bytes(ecs_def_comp_count(def)) + 1][]
  DynArray      entities; // EcsBufferEntity[] (Sorted on the .id field).
  Allocator*    compDataAllocator;
} EcsBuffer;

EcsBuffer ecs_buffer_create(Allocator*, const EcsDef* def);
void      ecs_buffer_destroy(EcsBuffer*);
void      ecs_buffer_clear(EcsBuffer*);

void ecs_buffer_destroy_entity(EcsBuffer*, EcsEntityId);
Mem  ecs_buffer_add_comp(EcsBuffer*, EcsEntityId, EcsCompId);
void ecs_buffer_remove_comp(EcsBuffer*, EcsEntityId, EcsCompId);

usize                ecs_buffer_count(const EcsBuffer*);
EcsEntityId          ecs_buffer_entity(const EcsBuffer*, usize index);
EcsBufferEntityFlags ecs_buffer_entity_flags(const EcsBuffer*, usize index);
BitSet               ecs_buffer_entity_added(const EcsBuffer*, usize index);
BitSet               ecs_buffer_entity_removed(const EcsBuffer*, usize index);
Mem                  ecs_buffer_entity_comp(const EcsBuffer*, usize index, EcsCompId);
