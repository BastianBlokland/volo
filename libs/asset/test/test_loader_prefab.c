#include "asset.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "ecs.h"

#include "utils_internal.h"

typedef struct {
  String name;
} TestPrefabData;

static const struct {
  String         id;
  String         text;
  TestPrefabData prefabs[2];
  usize          prefabCount;
} g_testData[] = {
    {
        .id          = string_static("empty.pfb"),
        .text        = string_static("{\"prefabs\": []}"),
        .prefabs     = {0},
        .prefabCount = 0,
    },
    {
        .id   = string_static("single.pfb"),
        .text = string_static("{ \"prefabs\": [ {"
                              "      \"name\": \"Unit\","
                              "      \"traits\": []"
                              "  }"
                              "]}"),
        .prefabs =
            {
                {.name = string_static("Unit")},
            },
        .prefabCount = 1,
    },
    {
        .id   = string_static("multi.pfb"),
        .text = string_static("{ \"prefabs\": [ {"
                              "      \"name\": \"UnitA\","
                              "      \"traits\": []"
                              "  }, {"
                              "      \"name\": \"UnitB\","
                              "      \"traits\": []"
                              "  }"
                              "]}"),
        .prefabs =
            {
                {.name = string_static("UnitA")},
                {.name = string_static("UnitB")},
            },
        .prefabCount = 2,
    },
    {
        .id   = string_static("trait-movement.pfb"),
        .text = string_static("{ \"prefabs\": [ {"
                              "      \"name\": \"Unit\","
                              "      \"traits\": [ {"
                              "        \"$type\": \"AssetPrefabTrait_Movement\","
                              "        \"speed\": 1"
                              "      }]"
                              "  }"
                              "]}"),
        .prefabs =
            {
                {.name = string_static("Unit")},
            },
        .prefabCount = 1,
    },
};

static const struct {
  String id;
  String text;
} g_errorTestData[] = {
    {
        .id   = string_static("invalid-json.pfb"),
        .text = string_static("Hello World"),
    },
    {
        .id   = string_static("duplicate-prefab-name.pfb"),
        .text = string_static("{ \"prefabs\": [ {"
                              "      \"name\": \"Unit\","
                              "      \"traits\": []"
                              "    }, {"
                              "      \"name\": \"Unit\","
                              "      \"traits\": []"
                              "    }"
                              "]}"),
    },
};

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(AssetView) { ecs_access_read(AssetPrefabMapComp); }

ecs_module_init(loader_prefab_test_module) {
  ecs_register_view(ManagerView);
  ecs_register_view(AssetView);
}

spec(loader_prefab) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    asset_register(def);
    ecs_register_module(def, loader_prefab_test_module);

    world  = ecs_world_create(g_alloc_heap, def);
    runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_None);
  }

  it("can load prefab maps") {
    AssetMemRecord records[array_elems(g_testData)];
    for (usize i = 0; i != array_elems(g_testData); ++i) {
      records[i] = (AssetMemRecord){.id = g_testData[i].id, .data = g_testData[i].text};
    }
    asset_manager_create_mem(world, AssetManagerFlags_None, records, array_elems(g_testData));
    ecs_world_flush(world);

    for (usize i = 0; i != array_elems(g_testData); ++i) {
      AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
      const EcsEntityId asset   = asset_lookup(world, manager, records[i].id);
      asset_acquire(world, asset);

      asset_test_wait(runner);

      check_require_msg(
          ecs_world_has_t(world, asset, AssetLoadedComp),
          "Failed to load: {}",
          fmt_text(g_testData[i].id));

      const AssetPrefabMapComp* map = ecs_utils_read_t(world, AssetView, asset, AssetPrefabMapComp);
      check_require(map->prefabCount == g_testData[i].prefabCount);
      for (usize a = 0; a != g_testData[i].prefabCount; ++a) {
        const AssetPrefab*    actualPrefab   = &map->prefabs[a];
        const TestPrefabData* expectedPrefab = &g_testData[i].prefabs[a];

        check_eq_int(actualPrefab->nameHash, string_hash(expectedPrefab->name));
      }
    }
  }

  it("can unload prefab-map assets") {
    const AssetMemRecord record = {.id = string_lit("test.pfb"), .data = g_testData[1].text};
    asset_manager_create_mem(world, AssetManagerFlags_None, &record, 1);
    ecs_world_flush(world);

    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
    const EcsEntityId asset   = asset_lookup(world, manager, string_lit("test.pfb"));

    asset_acquire(world, asset);
    asset_test_wait(runner);
    check(ecs_world_has_t(world, asset, AssetPrefabMapComp));

    asset_release(world, asset);
    asset_test_wait(runner);
    check(!ecs_world_has_t(world, asset, AssetPrefabMapComp));
  }

  it("fails when loading invalid prefab map files") {
    AssetMemRecord records[array_elems(g_errorTestData)];
    for (usize i = 0; i != array_elems(g_errorTestData); ++i) {
      records[i] = (AssetMemRecord){.id = g_errorTestData[i].id, .data = g_errorTestData[i].text};
    }
    asset_manager_create_mem(world, AssetManagerFlags_None, records, array_elems(g_errorTestData));
    ecs_world_flush(world);

    for (usize i = 0; i != array_elems(g_errorTestData); ++i) {
      AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
      const EcsEntityId asset   = asset_lookup(world, manager, records[i].id);
      asset_acquire(world, asset);
      asset_test_wait(runner);

      check(ecs_world_has_t(world, asset, AssetFailedComp));
      check(!ecs_world_has_t(world, asset, AssetPrefabMapComp));
    }
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
