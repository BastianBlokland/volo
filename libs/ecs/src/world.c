#include "core_alloc.h"

#include "def_internal.h"
#include "storage_internal.h"
#include "world_internal.h"

struct sEcsWorld {
  const EcsDef* def;
  EcsStorage    storage;
  Allocator*    alloc;
};

EcsWorld* ecs_world_create(Allocator* alloc, const EcsDef* def) {
  ecs_def_freeze((EcsDef*)def);

  EcsWorld* world = alloc_alloc_t(alloc, EcsWorld);
  *world          = (EcsWorld){
      .def     = def,
      .storage = ecs_storage_create(alloc, def),
      .alloc   = alloc,
  };

  return world;
}

void ecs_world_destroy(EcsWorld* world) {
  ecs_def_unfreeze((EcsDef*)world->def);

  ecs_storage_destroy(&world->storage);
  alloc_free_t(world->alloc, world);
}

const EcsDef* ecs_world_def(EcsWorld* world) { return world->def; }

EcsEntityId ecs_world_entity_create(EcsWorld* world) {
  return ecs_storage_entity_create(&world->storage);
}

bool ecs_world_entity_exists(const EcsWorld* world, const EcsEntityId id) {
  return ecs_storage_entity_exists(&world->storage, id);
}

void ecs_world_flush(EcsWorld* world) { (void)world; }
