#include "core_alloc.h"
#include "ecs_world.h"

#include "def_internal.h"

struct sEcsWorld {
  const EcsDef* def;
  Allocator*    alloc;
};

EcsWorld* ecs_world_create(Allocator* alloc, const EcsDef* def) {
  ecs_def_freeze((EcsDef*)def);

  EcsWorld* world = alloc_alloc_t(alloc, EcsWorld);
  *world          = (EcsWorld){
      .def   = def,
      .alloc = alloc,
  };
  return world;
}

void ecs_world_destroy(EcsWorld* world) { alloc_free_t(world->alloc, world); }

const EcsDef* ecs_world_def(EcsWorld* world) { return world->def; }
