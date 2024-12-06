#include "asset.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "ecs_utils.h"
#include "ecs_world.h"

#include "utils_internal.h"

static const struct {
  String id;
  String text;
} g_testData[] = {
    {
        .id   = string_static("empty.products"),
        .text = string_static("{\"sets\": []}"),
    },
    {
        .id   = string_static("test.products"),
        .text = string_static("{ \"sets\": [ {"
                              "      \"name\": \"A\","
                              "      \"products\": ["
                              "        { \"$type\": \"AssetProduct_Unit\","
                              "          \"unitPrefab\": \"InfantryRifle\" }"
                              "      ]"
                              "    }, {"
                              "      \"name\": \"B\","
                              "      \"products\": ["
                              "        { \"$type\": \"AssetProduct_Unit\","
                              "          \"unitPrefab\": \"InfantryRifle\" }"
                              "      ]"
                              "    }"
                              "]}"),
    },

};

static const struct {
  String id;
  String text;
} g_errorTestData[] = {
    {
        .id   = string_static("invalid-json.products"),
        .text = string_static("Hello World"),
    },
    {
        .id   = string_static("duplicate-set-name.products"),
        .text = string_static("{ \"sets\": [ {"
                              "      \"name\": \"A\","
                              "      \"products\": ["
                              "        { \"$type\": \"AssetProduct_Unit\","
                              "          \"unitPrefab\": \"InfantryRifle\" }"
                              "      ]"
                              "    }, {"
                              "      \"name\": \"A\","
                              "      \"products\": ["
                              "        { \"$type\": \"AssetProduct_Unit\","
                              "          \"unitPrefab\": \"InfantryRifle\" }"
                              "      ]"
                              "    }"
                              "]}"),
    },
};

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(AssetView) { ecs_access_read(AssetProductMapComp); }

ecs_module_init(loader_product_test_module) {
  ecs_register_view(ManagerView);
  ecs_register_view(AssetView);
}

spec(loader_product) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_allocHeap);
    asset_register(def);
    ecs_register_module(def, loader_product_test_module);

    world  = ecs_world_create(g_allocHeap, def);
    runner = ecs_runner_create(g_allocHeap, world, EcsRunnerFlags_None);
  }

  it("can load product maps") {
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

      const bool valid = ecs_world_has_t(world, asset, AssetLoadedComp) &&
                         ecs_world_has_t(world, asset, AssetProductMapComp);
      check_require_msg(valid, "Failed to load: {}", fmt_text(g_testData[i].id));
    }
  }

  it("can unload product-map assets") {
    const AssetMemRecord record = {.id = string_lit("empty.products"), .data = g_testData[0].text};
    asset_manager_create_mem(world, AssetManagerFlags_None, &record, 1);
    ecs_world_flush(world);

    EcsEntityId asset;
    {
      AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
      asset                     = asset_lookup(world, manager, string_lit("empty.products"));
    }
    asset_acquire(world, asset);

    asset_test_wait(runner);
    check(ecs_world_has_t(world, asset, AssetProductMapComp));

    asset_release(world, asset);
    asset_test_wait(runner);
    check(!ecs_world_has_t(world, asset, AssetProductMapComp));
  }

  it("fails when loading invalid product-map files") {
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
      check(!ecs_world_has_t(world, asset, AssetProductMapComp));
    }
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
