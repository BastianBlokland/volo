#include "asset.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "ecs_utils.h"
#include "ecs_world.h"

#include "utils_internal.h"

static const AssetMemRecord g_testData[] = {
    {
        .id   = string_static("test.terrain"),
        .data = string_static("{"
                              "  \"graphicId\": \"test.graphic\","
                              "  \"heightmapId\": \"height.r16\","
                              "  \"size\": 100.0,"
                              "  \"playSize\": 50.0,"
                              "  \"heightMax\": 1.0"
                              "}"),
    },
};

static const AssetMemRecord g_errorTestData[] = {
    {
        .id   = string_static("empty.terrain"),
        .data = string_static("{}"),
    },
};

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(AssetView) { ecs_access_read(AssetTerrainComp); }

ecs_module_init(loader_terrain_test_module) {
  ecs_register_view(ManagerView);
  ecs_register_view(AssetView);
}

spec(loader_terrain) {
  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_allocHeap);
    asset_register(def);
    ecs_register_module(def, loader_terrain_test_module);

    world  = ecs_world_create(g_allocHeap, def);
    runner = ecs_runner_create(g_allocHeap, world, EcsRunnerFlags_None);
  }

  it("can load terrain assets") {
    asset_manager_create_mem(world, AssetManagerFlags_None, g_testData, array_elems(g_testData));
    ecs_world_flush(world);

    EcsEntityId asset;
    {
      AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
      asset                     = asset_lookup(world, manager, string_lit("test.terrain"));
    }
    asset_acquire(world, asset);

    asset_test_wait(runner);

    check_require(ecs_world_has_t(world, asset, AssetLoadedComp));
    const AssetTerrainComp* terrain = ecs_utils_read_t(world, AssetView, asset, AssetTerrainComp);

    check_eq_string(terrain->graphicId, string_lit("test.graphic"));
    check_eq_string(terrain->heightmapId, string_lit("height.r16"));
  }

  it("can unload terrain assets") {
    asset_manager_create_mem(world, AssetManagerFlags_None, g_testData, array_elems(g_testData));
    ecs_world_flush(world);

    EcsEntityId asset;
    {
      AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
      asset                     = asset_lookup(world, manager, string_lit("test.terrain"));
    }
    asset_acquire(world, asset);

    asset_test_wait(runner);

    check(ecs_world_has_t(world, asset, AssetTerrainComp));

    asset_release(world, asset);
    asset_test_wait(runner);

    check(!ecs_world_has_t(world, asset, AssetTerrainComp));
  }

  it("fails when loading invalid terrain files") {
    asset_manager_create_mem(
        world, AssetManagerFlags_None, g_errorTestData, array_elems(g_errorTestData));
    ecs_world_flush(world);

    for (usize i = 0; i != array_elems(g_errorTestData); ++i) {
      EcsEntityId asset;
      {
        AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
        asset                     = asset_lookup(world, manager, g_errorTestData[i].id);
      }
      asset_acquire(world, asset);

      asset_test_wait(runner);

      check(ecs_world_has_t(world, asset, AssetFailedComp));
      check(!ecs_world_has_t(world, asset, AssetTerrainComp));
    }
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
