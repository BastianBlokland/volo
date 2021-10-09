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

typedef struct sEcsBufferCompData EcsBufferCompData;

EcsBuffer ecs_buffer_create(Allocator*, const EcsDef* def);
void      ecs_buffer_destroy(EcsBuffer*);
void      ecs_buffer_clear(EcsBuffer*);

void  ecs_buffer_destroy_entity(EcsBuffer*, EcsEntityId);
void* ecs_buffer_comp_add(EcsBuffer*, EcsEntityId, EcsCompId, Mem data);
void  ecs_buffer_comp_remove(EcsBuffer*, EcsEntityId, EcsCompId);

usize                ecs_buffer_count(const EcsBuffer*);
EcsEntityId          ecs_buffer_entity(const EcsBuffer*, usize index);
EcsBufferEntityFlags ecs_buffer_entity_flags(const EcsBuffer*, usize index);
BitSet               ecs_buffer_entity_added(const EcsBuffer*, usize index);
BitSet               ecs_buffer_entity_removed(const EcsBuffer*, usize index);
EcsBufferCompData*   ecs_buffer_comp_begin(const EcsBuffer*, usize index);
EcsBufferCompData*   ecs_buffer_comp_next(const EcsBufferCompData*);
EcsCompId            ecs_buffer_comp_id(const EcsBufferCompData*);
Mem                  ecs_buffer_comp_data(const EcsBuffer*, const EcsBufferCompData*);
