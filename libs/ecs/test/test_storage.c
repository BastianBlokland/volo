#include "check_spec.h"
#include "ecs_storage.h"

spec(storage) {

  EcsStorage* storage = null;

  setup() { storage = ecs_storage_create(g_alloc_heap); }

  it("can create new entities") {
    const EcsEntityId entity = ecs_storage_entity_create(storage);
    check(entity != 0);
  }

  teardown() { ecs_storage_destroy(storage); }
}
