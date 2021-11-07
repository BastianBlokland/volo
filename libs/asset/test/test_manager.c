#include "asset.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "ecs.h"

static const AssetMemRecord records[] = {
    {.id = string_static("a.raw"), .data = string_static("Hello")},
    {.id = string_static("b.raw"), .data = string_static("World")},
};

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }

static AssetManagerComp* asset_manager_get(EcsWorld* world) {
  EcsIterator* itr = ecs_view_itr_first(ecs_world_view_t(world, ManagerView));
  return ecs_view_write_t(itr, AssetManagerComp);
}

static void asset_manager_wait(EcsRunner* runner) {
  /**
   * Due to the asynchronous nature of asset-loading we give the asset-system a set number of ticks
   * to process requests.
   */
  static const u32 numTicks = 3;
  for (u32 i = 0; i != numTicks; ++i) {
    ecs_run_sync(runner);
  }
}

ecs_module_init(manager_test_module) { ecs_register_view(ManagerView); }

spec(manager) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    asset_register(def);
    ecs_register_module(def, manager_test_module);

    world = ecs_world_create(g_alloc_heap, def);
    asset_manager_create_mem(world, records, array_elems(records));
    ecs_world_flush(world);

    runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_None);
  }

  it("can lookup assets by name") {
    AssetManagerComp* manager = asset_manager_get(world);

    const EcsEntityId assetA = asset_lookup(world, manager, string_lit("a.raw"));
    check(assetA != 0);
    check_eq_int(asset_lookup(world, manager, string_lit("a.raw")), assetA);

    const EcsEntityId assetB = asset_lookup(world, manager, string_lit("b.raw"));
    check(assetB != 0);
    check(assetA != assetB);
    check_eq_int(asset_lookup(world, manager, string_lit("b.raw")), assetB);
  }

  it("loads assets when they are acquired") {
    AssetManagerComp* manager = asset_manager_get(world);
    const EcsEntityId asset   = asset_lookup(world, manager, string_lit("a.raw"));
    asset_acquire(world, asset);

    asset_manager_wait(runner);

    check(ecs_world_has_t(world, asset, AssetLoadedComp));
    check(ecs_world_has_t(world, asset, AssetRawComp));
  }

  it("unloads assets when they are released") {
    AssetManagerComp* manager = asset_manager_get(world);
    const EcsEntityId asset   = asset_lookup(world, manager, string_lit("a.raw"));
    asset_acquire(world, asset);

    asset_manager_wait(runner);

    check(ecs_world_has_t(world, asset, AssetComp));
    check(ecs_world_has_t(world, asset, AssetLoadedComp));
    check(ecs_world_has_t(world, asset, AssetRawComp));

    asset_release(world, asset);

    asset_manager_wait(runner);

    ecs_run_sync(runner);
    check(ecs_world_has_t(world, asset, AssetComp));
    check(!ecs_world_has_t(world, asset, AssetLoadedComp));
    check(!ecs_world_has_t(world, asset, AssetRawComp));
  }

  it("keeps assets loaded as long as any acquire is still active") {
    AssetManagerComp* manager = asset_manager_get(world);
    const EcsEntityId asset   = asset_lookup(world, manager, string_lit("a.raw"));
    asset_acquire(world, asset);
    asset_acquire(world, asset);

    asset_manager_wait(runner);

    asset_release(world, asset);

    ecs_run_sync(runner);
    check(ecs_world_has_t(world, asset, AssetLoadedComp));
    check(ecs_world_has_t(world, asset, AssetRawComp));

    asset_release(world, asset);

    asset_manager_wait(runner);

    check(!ecs_world_has_t(world, asset, AssetLoadedComp));
    check(!ecs_world_has_t(world, asset, AssetRawComp));
  }

  it("ignores acquires immediately followed by releases") {
    AssetManagerComp* manager = asset_manager_get(world);
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
    AssetManagerComp* manager = asset_manager_get(world);
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

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
