#include "asset.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "ecs.h"

#include "utils_internal.h"

static const struct {
  String           id;
  String           text;
  AssetLevelObject objects[2];
  usize            objectCount;
} g_testData[] = {
    {
        .id          = string_static("empty.level"),
        .text        = string_static("{\"objects\": []}"),
        .objects     = {0},
        .objectCount = 0,
    },
    {
        .id   = string_static("single.level"),
        .text = string_static("{ \"objects\": [ {"
                              "      \"prefab\": \"Unit\","
                              "      \"faction\": \"A\","
                              "      \"position\": { \"x\": 42 },"
                              "      \"rotation\": { \"x\": 0, \"y\": 0, \"z\": 0, \"w\": 1 }"
                              "  }"
                              "]}"),
        .objects =
            {
                {
                    .prefab   = 1470434201,
                    .faction  = AssetLevelFaction_A,
                    .position = {.x = 42},
                    .rotation = {0},
                },
            },
        .objectCount = 1,
    },
};

static const struct {
  String id;
  String text;
} g_errorTestData[] = {
    {
        .id   = string_static("invalid-json.level"),
        .text = string_static("Hello World"),
    },
};

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(AssetView) { ecs_access_read(AssetLevelComp); }

ecs_module_init(loader_level_test_module) {
  ecs_register_view(ManagerView);
  ecs_register_view(AssetView);
}

spec(loader_level) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_allocHeap);
    asset_register(def);
    ecs_register_module(def, loader_level_test_module);

    world  = ecs_world_create(g_allocHeap, def);
    runner = ecs_runner_create(g_allocHeap, world, EcsRunnerFlags_None);
  }

  it("can load levels") {
    AssetMemRecord records[array_elems(g_testData)];
    for (usize i = 0; i != array_elems(g_testData); ++i) {
      records[i] = (AssetMemRecord){.id = g_testData[i].id, .data = g_testData[i].text};
    }
    asset_manager_create_mem(world, AssetManagerFlags_None, records, array_elems(g_testData));
    ecs_world_flush(world);

    for (usize i = 0; i != array_elems(g_testData); ++i) {
      EcsEntityId asset;
      {
        AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
        asset                     = asset_lookup(world, manager, records[i].id);
      }
      asset_acquire(world, asset);

      asset_test_wait(runner);

      check_require_msg(
          ecs_world_has_t(world, asset, AssetLoadedComp),
          "Failed to load: {}",
          fmt_text(g_testData[i].id));

      const AssetLevel* lvl = &ecs_utils_read_t(world, AssetView, asset, AssetLevelComp)->level;
      check_require(lvl->objects.count == g_testData[i].objectCount);
      for (usize a = 0; a != g_testData[i].objectCount; ++a) {
        const AssetLevelObject* actualObject   = &lvl->objects.values[a];
        const AssetLevelObject* expectedObject = &g_testData[i].objects[a];

        check_eq_int(actualObject->prefab, expectedObject->prefab);
        check_eq_int(actualObject->faction, expectedObject->faction);
        check_eq_float(actualObject->position.x, expectedObject->position.x, 1e-4f);
        check_eq_float(actualObject->position.y, expectedObject->position.y, 1e-4f);
        check_eq_float(actualObject->position.z, expectedObject->position.z, 1e-4f);
        check_eq_float(actualObject->rotation.x, expectedObject->rotation.x, 1e-4f);
        check_eq_float(actualObject->rotation.y, expectedObject->rotation.y, 1e-4f);
        check_eq_float(actualObject->rotation.z, expectedObject->rotation.z, 1e-4f);
      }
    }
  }

  it("can unload level assets") {
    const AssetMemRecord record = {.id = string_lit("test.level"), .data = g_testData[1].text};
    asset_manager_create_mem(world, AssetManagerFlags_None, &record, 1);
    ecs_world_flush(world);

    EcsEntityId asset;
    {
      AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
      asset                     = asset_lookup(world, manager, string_lit("test.level"));
    }
    asset_acquire(world, asset);

    asset_test_wait(runner);
    check(ecs_world_has_t(world, asset, AssetLevelComp));

    asset_release(world, asset);
    asset_test_wait(runner);
    check(!ecs_world_has_t(world, asset, AssetLevelComp));
  }

  it("fails when loading invalid level files") {
    AssetMemRecord records[array_elems(g_errorTestData)];
    for (usize i = 0; i != array_elems(g_errorTestData); ++i) {
      records[i] = (AssetMemRecord){.id = g_errorTestData[i].id, .data = g_errorTestData[i].text};
    }
    asset_manager_create_mem(world, AssetManagerFlags_None, records, array_elems(g_errorTestData));
    ecs_world_flush(world);

    for (usize i = 0; i != array_elems(g_errorTestData); ++i) {
      EcsEntityId asset;
      {
        AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
        asset                     = asset_lookup(world, manager, records[i].id);
      }
      asset_acquire(world, asset);

      asset_test_wait(runner);

      check(ecs_world_has_t(world, asset, AssetFailedComp));
      check(!ecs_world_has_t(world, asset, AssetLevelComp));
    }
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
