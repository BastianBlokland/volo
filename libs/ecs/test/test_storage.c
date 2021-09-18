#include "check_spec.h"
#include "ecs_storage.h"

spec(storage) {

  EcsMeta*    meta    = null;
  EcsStorage* storage = null;

  setup() {
    meta    = ecs_meta_create(g_alloc_heap);
    storage = ecs_storage_create(g_alloc_heap, meta);
  }

  it("can create new entities") {
    const EcsEntityId entity = ecs_storage_entity_create(storage);
    check(ecs_storage_entity_exists(storage, entity));
  }

  teardown() {
    ecs_meta_destroy(meta);
    ecs_storage_destroy(storage);
  }
}
