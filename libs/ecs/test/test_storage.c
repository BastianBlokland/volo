#include "check_spec.h"
#include "core_alloc.h"
#include "ecs_storage.h"

spec(storage) {

  EcsDef*     def     = null;
  EcsStorage* storage = null;

  setup() {
    def     = ecs_def_create(g_alloc_heap);
    storage = ecs_storage_create(g_alloc_heap, def);
  }

  it("can create new entities") {
    const EcsEntityId entity = ecs_storage_entity_create(storage);
    check(ecs_storage_entity_exists(storage, entity));
  }

  teardown() {
    ecs_def_destroy(def);
    ecs_storage_destroy(storage);
  }
}
