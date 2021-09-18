#include "ecs_storage.h"

#include "entity_allocator_internal.h"

struct sEcsStorage {
  EntityAllocator entityAllocator;
  Allocator*      memoryAllocator;
};
