#include "asset.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "ecs.h"

#include "utils_internal.h"

static const struct {
  String            id;
  String            text;
  AssetBehaviorType type;
} g_testData[] = {
    {
        .id   = string_static("success.bt"),
        .text = string_static("{ \"$type\": \"AssetBehaviorType_Success\" }"),
        .type = AssetBehaviorType_Success,
    },
    {
        .id   = string_static("invert.bt"),
        .text = string_static("{\n"
                              "\"$type\": \"AssetBehaviorType_Invert\",\n"
                              "\"child\": { \"$type\": \"AssetBehaviorType_Failure\" }\n"
                              "}"),
        .type = AssetBehaviorType_Invert,
    },

};

static const struct {
  String id;
  String text;
} g_errorTestData[] = {
    {
        .id   = string_static("invalid.bt"),
        .text = string_static("Hello World"),
    },
};

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(AssetView) { ecs_access_read(AssetBehaviorComp); }

ecs_module_init(loader_behavior_test_module) {
  ecs_register_view(ManagerView);
  ecs_register_view(AssetView);
}

spec(loader_behavior) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    asset_register(def);
    ecs_register_module(def, loader_behavior_test_module);

    world  = ecs_world_create(g_alloc_heap, def);
    runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_None);
  }

  it("can load behavior assets") {
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

      check_require(ecs_world_has_t(world, asset, AssetLoadedComp));
      const AssetBehaviorComp* comp = ecs_utils_read_t(world, AssetView, asset, AssetBehaviorComp);

      check_eq_int(comp->root.type, g_testData[i].type);
    }
  }

  it("can unload behavior assets") {
    const AssetMemRecord record = {.id = string_lit("test.bt"), .data = g_testData[0].text};
    asset_manager_create_mem(world, AssetManagerFlags_None, &record, 1);
    ecs_world_flush(world);

    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
    const EcsEntityId asset   = asset_lookup(world, manager, string_lit("test.bt"));

    asset_acquire(world, asset);
    asset_test_wait(runner);
    check(ecs_world_has_t(world, asset, AssetBehaviorComp));

    asset_release(world, asset);
    asset_test_wait(runner);
    check(!ecs_world_has_t(world, asset, AssetBehaviorComp));
  }

  it("fails when loading invalid behavior assets") {
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
      check(!ecs_world_has_t(world, asset, AssetBehaviorComp));
    }
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
