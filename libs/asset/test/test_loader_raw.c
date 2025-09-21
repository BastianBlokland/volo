#include "asset/manager.h"
#include "asset/raw.h"
#include "asset/register.h"
#include "check/spec.h"
#include "core/alloc.h"
#include "core/array.h"
#include "ecs/utils.h"
#include "ecs/world.h"

#include "utils.h"

static const AssetMemRecord g_records[] = {
    {.id = string_static("a.raw"), .data = string_static("Hello World")},
    {.id = string_static("b.bin"), .data = string_static("Hello World")},
};

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(AssetView) { ecs_access_read(AssetRawComp); }

ecs_module_init(loader_raw_test_module) {
  ecs_register_view(ManagerView);
  ecs_register_view(AssetView);
}

spec(loader_raw) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_allocHeap);
    asset_register(def, &(AssetRegisterContext){0});
    ecs_register_module(def, loader_raw_test_module);

    world = ecs_world_create(g_allocHeap, def);
    asset_manager_create_mem(world, AssetManagerFlags_None, g_records, array_elems(g_records));
    ecs_world_flush(world);

    runner = ecs_runner_create(g_allocHeap, world, EcsRunnerFlags_None);
  }

  it("can load raw assets") {
    array_for_t(g_records, AssetMemRecord, record) {
      EcsEntityId asset;
      {
        AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
        asset                     = asset_lookup(world, manager, record->id);
      }
      asset_acquire(world, asset);

      asset_test_wait(runner);

      check_require(ecs_world_has_t(world, asset, AssetLoadedComp));
      const AssetRawComp* assetRaw = ecs_utils_read_t(world, AssetView, asset, AssetRawComp);
      check_eq_string(assetRaw->data, record->data);
    }
  }

  it("can unload raw assets") {
    EcsEntityId asset;
    {
      AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
      asset                     = asset_lookup(world, manager, string_lit("a.raw"));
    }
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
