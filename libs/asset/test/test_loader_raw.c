#include "asset.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "ecs.h"

#include "utils_internal.h"

static const AssetMemRecord records[] = {
    {.id = string_static("a.raw"), .data = string_static("Hello World")},
};

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(AssetRawView) { ecs_access_read(AssetRawComp); }

static const AssetRawComp* asset_raw_get(EcsWorld* world, const EcsEntityId assetEntity) {
  EcsIterator* itr = ecs_view_itr_at(ecs_world_view_t(world, AssetRawView), assetEntity);
  return ecs_view_read_t(itr, AssetRawComp);
}

ecs_module_init(loader_raw_test_module) {
  ecs_register_view(ManagerView);
  ecs_register_view(AssetRawView);
}

spec(loader_raw) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    asset_register(def);
    ecs_register_module(def, loader_raw_test_module);

    world = ecs_world_create(g_alloc_heap, def);
    asset_manager_create_mem(world, records, array_elems(records));
    ecs_world_flush(world);

    runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_None);
  }

  it("can load raw assets") {
    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);

    const EcsEntityId asset = asset_lookup(world, manager, string_lit("a.raw"));
    asset_acquire(world, asset);

    asset_test_wait(runner);

    check_eq_string(asset_raw_get(world, asset)->data, string_lit("Hello World"));
  }

  it("can unload raw assets") {
    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);

    const EcsEntityId asset = asset_lookup(world, manager, string_lit("a.raw"));

    asset_acquire(world, asset);
    asset_test_wait(runner);
    check(ecs_world_has_t(world, asset, AssetRawComp));

    asset_release(world, asset);
    asset_test_wait(runner);
    check(!ecs_world_has_t(world, asset, AssetRawComp));
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
