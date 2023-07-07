#include "asset.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "ecs.h"
#include "ecs_view.h"

#include "utils_internal.h"

static const AssetMemRecord g_records[] = {
    {.id = string_static("a.raw"), .data = string_static("Hello")},
    {.id = string_static("b.raw"), .data = string_static("World")},
};

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(AssetView) { ecs_access_read(AssetComp); }

ecs_module_init(manager_test_module) {
  ecs_register_view(ManagerView);
  ecs_register_view(AssetView);
}

spec(manager) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    asset_register(def);
    ecs_register_module(def, manager_test_module);

    world = ecs_world_create(g_alloc_heap, def);
    asset_manager_create_mem(world, AssetManagerFlags_None, g_records, array_elems(g_records));
    ecs_world_flush(world);

    runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_None);
  }

  it("can lookup assets by name") {
    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);

    const EcsEntityId assetA = asset_lookup(world, manager, string_lit("a.raw"));
    check(assetA != 0);
    check_eq_int(asset_lookup(world, manager, string_lit("a.raw")), assetA);

    const EcsEntityId assetB = asset_lookup(world, manager, string_lit("b.raw"));
    check(assetB != 0);
    check(assetA != assetB);
    check_eq_int(asset_lookup(world, manager, string_lit("b.raw")), assetB);
  }

  it("loads assets when they are acquired") {
    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
    const EcsEntityId asset   = asset_lookup(world, manager, string_lit("a.raw"));
    asset_acquire(world, asset);

    asset_test_wait(runner);

    check(ecs_world_has_t(world, asset, AssetLoadedComp));
    check(!ecs_world_has_t(world, asset, AssetFailedComp));
    check(ecs_world_has_t(world, asset, AssetRawComp));
  }

  it("unloads assets when they are released") {
    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
    const EcsEntityId asset   = asset_lookup(world, manager, string_lit("a.raw"));
    asset_acquire(world, asset);

    asset_test_wait(runner);

    check(ecs_world_has_t(world, asset, AssetComp));
    check(ecs_world_has_t(world, asset, AssetLoadedComp));
    check(ecs_world_has_t(world, asset, AssetRawComp));

    asset_release(world, asset);

    asset_test_wait(runner);

    ecs_run_sync(runner);
    check(ecs_world_has_t(world, asset, AssetComp));
    check(!ecs_world_has_t(world, asset, AssetLoadedComp));
    check(!ecs_world_has_t(world, asset, AssetRawComp));
  }

  it("keeps assets loaded as long as any acquire is still active") {
    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
    const EcsEntityId asset   = asset_lookup(world, manager, string_lit("a.raw"));
    asset_acquire(world, asset);
    asset_acquire(world, asset);

    asset_test_wait(runner);

    asset_release(world, asset);

    ecs_run_sync(runner);
    check(ecs_world_has_t(world, asset, AssetLoadedComp));
    check(ecs_world_has_t(world, asset, AssetRawComp));

    asset_release(world, asset);

    asset_test_wait(runner);

    check(!ecs_world_has_t(world, asset, AssetLoadedComp));
    check(!ecs_world_has_t(world, asset, AssetRawComp));
  }

  it("ignores acquires immediately followed by releases") {
    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
    const EcsEntityId asset   = asset_lookup(world, manager, string_lit("a.raw"));
    asset_acquire(world, asset);
    asset_acquire(world, asset);
    asset_release(world, asset);
    asset_release(world, asset);

    ecs_run_sync(runner);
    ecs_run_sync(runner);
    check(!ecs_world_has_t(world, asset, AssetLoadedComp));
    check(!ecs_world_has_t(world, asset, AssetRawComp));
  }

  it("supports multiple simultaneous loads") {
    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
    const EcsEntityId assetA  = asset_lookup(world, manager, string_lit("a.raw"));
    const EcsEntityId assetB  = asset_lookup(world, manager, string_lit("b.raw"));

    asset_acquire(world, assetA);
    asset_acquire(world, assetB);

    ecs_run_sync(runner);
    ecs_run_sync(runner);

    check(ecs_world_has_t(world, assetA, AssetLoadedComp));
    check(ecs_world_has_t(world, assetA, AssetRawComp));

    check(ecs_world_has_t(world, assetB, AssetRawComp));
    check(ecs_world_has_t(world, assetB, AssetLoadedComp));
  }

  it("fails loads for non-existing assets") {
    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
    const EcsEntityId entity  = asset_lookup(world, manager, string_lit("non-existent"));

    asset_acquire(world, entity);

    ecs_run_sync(runner);
    ecs_run_sync(runner);

    check(ecs_world_has_t(world, entity, AssetFailedComp));
    check(!ecs_world_has_t(world, entity, AssetLoadedComp));
  }

  it("can retrieve the identifier of loaded assets") {
    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);

    const EcsEntityId entity = asset_lookup(world, manager, string_lit("a.raw"));
    ecs_run_sync(runner);

    const AssetComp* comp = ecs_utils_read_t(world, AssetView, entity, AssetComp);
    check_eq_string(asset_id(comp), string_lit("a.raw"));
  }

  it("delays load-after-unload by one frame") {
    /**
     * Regression test for when the asset cleanup systems runs in the same frame as the new load,
     * causing a crash due to duplicate component add.
     */

    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);

    const EcsEntityId entity = asset_lookup(world, manager, string_lit("a.raw"));
    asset_acquire(world, entity);
    ecs_world_flush(world);

    ecs_run_sync(runner);

    check(ecs_world_has_t(world, entity, AssetLoadedComp));
    check(ecs_world_has_t(world, entity, AssetRawComp));

    asset_release(world, entity);

    ecs_run_sync(runner);

    asset_acquire(world, entity);

    ecs_run_sync(runner);
    ecs_run_sync(runner);
    ecs_run_sync(runner);

    check(ecs_world_has_t(world, entity, AssetLoadedComp));
    check(ecs_world_has_t(world, entity, AssetRawComp));
  }

  it("clears the dirty state after loading") {
    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);

    const EcsEntityId entity = asset_lookup(world, manager, string_lit("a.raw"));
    asset_acquire(world, entity);
    ecs_world_flush(world);

    check(ecs_world_has_t(world, entity, AssetDirtyComp));

    asset_test_wait(runner);

    check(ecs_world_has_t(world, entity, AssetLoadedComp));
    check(ecs_world_has_t(world, entity, AssetRawComp));
    check(!ecs_world_has_t(world, entity, AssetDirtyComp));

    /**
     * When acquiring the asset when its already loaded the dirty state should be cleared
     * immediately.
     */
    asset_acquire(world, entity);
    ecs_world_flush(world);
    check(ecs_world_has_t(world, entity, AssetDirtyComp));
    ecs_run_sync(runner);
    check(!ecs_world_has_t(world, entity, AssetDirtyComp));
  }

  it("supports querying all assets with a wildcard") {
    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);

    EcsEntityId results[asset_query_max_results];
    const u32   resultCount = asset_query(world, manager, string_lit("*"), results);

    check_eq_int(resultCount, 2);

    const EcsEntityId entityA = asset_lookup(world, manager, string_lit("a.raw"));
    const EcsEntityId entityB = asset_lookup(world, manager, string_lit("b.raw"));

    check(results[0] != results[1]);
    check(results[0] == entityA || results[0] == entityB);
    check(results[1] == entityA || results[0] == entityB);
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
