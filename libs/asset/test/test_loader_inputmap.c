#include "asset.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "ecs.h"
#include "ecs_utils.h"

#include "utils_internal.h"

typedef struct {
  String            name;
  u32               blockerBits;
  AssetInputBinding bindings[2];
  usize             bindingCount;
} TestActionData;

static const struct {
  String         id;
  String         text;
  TestActionData actions[2];
  usize          actionCount;
} g_testData[] = {
    {
        .id          = string_static("empty.inputs"),
        .text        = string_static("{\"actions\": []}"),
        .actions     = {0},
        .actionCount = 0,
    },
    {
        .id   = string_static("single_binding.inputs"),
        .text = string_static("{ \"actions\": [ {"
                              "    \"name\": \"Jump\","
                              "    \"bindings\": [ {"
                              "       \"type\": \"Pressed\","
                              "       \"key\":  \"Space\""
                              "        } ]"
                              "     }"
                              "]}"),
        .actions =
            {
                {
                    .name         = string_static("Jump"),
                    .bindings     = {{.type = AssetInputType_Pressed, .key = 15}},
                    .bindingCount = 1,
                },
            },
        .actionCount = 1,
    },
    {
        .id   = string_static("multi_bindings.inputs"),
        .text = string_static("{ \"actions\": [ {"
                              "    \"name\": \"Jump\","
                              "    \"bindings\": [ {"
                              "       \"type\": \"Pressed\","
                              "       \"key\":  \"Space\""
                              "       }, {"
                              "         \"type\": \"Released\","
                              "         \"key\":  \"ArrowUp\""
                              "       } ]"
                              "    }"
                              "]}"),
        .actions =
            {
                {
                    .name = string_static("Jump"),
                    .bindings =
                        {{.type = AssetInputType_Pressed, .key = 15},
                         {.type = AssetInputType_Released, .key = 22}},
                    .bindingCount = 2,
                },
            },
        .actionCount = 1,
    },
    {
        .id   = string_static("multi_actions.inputs"),
        .text = string_static("{ \"actions\": [ {"
                              "    \"name\": \"Forward\","
                              "    \"bindings\": [ {"
                              "        \"type\": \"Down\","
                              "        \"key\":  \"W\""
                              "      } ]"
                              "    }, {"
                              "    \"name\": \"Backward\","
                              "    \"bindings\": [ {"
                              "       \"type\": \"Down\","
                              "       \"key\":  \"S\""
                              "      } ]"
                              "    }"
                              "]}"),
        .actions =
            {
                {
                    .name         = string_static("Forward"),
                    .bindings     = {{.type = AssetInputType_Down, .key = 50}},
                    .bindingCount = 1,
                },
                {
                    .name         = string_static("Backward"),
                    .bindings     = {{.type = AssetInputType_Down, .key = 46}},
                    .bindingCount = 1,
                },
            },
        .actionCount = 2,
    },
    {
        .id   = string_static("blockers.inputs"),
        .text = string_static("{ \"actions\": [ {"
                              "    \"name\": \"Jump\","
                              "    \"blockers\": [ \"HoveringUi\", \"TextInput\" ],"
                              "    \"bindings\": [ {"
                              "       \"type\": \"Pressed\","
                              "       \"key\":  \"Space\""
                              "        } ]"
                              "     }"
                              "]}"),
        .actions =
            {
                {
                    .name         = string_static("Jump"),
                    .blockerBits  = 0b11,
                    .bindings     = {{.type = AssetInputType_Pressed, .key = 15}},
                    .bindingCount = 1,
                },
            },
        .actionCount = 1,
    },
    {
        .id   = string_static("required-modifiers.inputs"),
        .text = string_static("{ \"actions\": [ {"
                              "    \"name\": \"Jump\","
                              "    \"bindings\": [ {"
                              "       \"type\": \"Pressed\","
                              "       \"key\":  \"Space\","
                              "       \"requiredModifiers\": [ \"Shift\", \"Control\" ],"
                              "        } ]"
                              "     }"
                              "]}"),
        .actions =
            {
                {
                    .name         = string_static("Jump"),
                    .bindings     = {{
                            .type                 = AssetInputType_Pressed,
                            .key                  = 15,
                            .requiredModifierBits = 0b11,
                    }},
                    .bindingCount = 1,
                },
            },
        .actionCount = 1,
    },
    {
        .id   = string_static("illegal-modifiers.inputs"),
        .text = string_static("{ \"actions\": [ {"
                              "    \"name\": \"Jump\","
                              "    \"bindings\": [ {"
                              "       \"type\": \"Pressed\","
                              "       \"key\":  \"Space\","
                              "       \"illegalModifiers\": [ \"Shift\", \"Control\" ],"
                              "        } ]"
                              "     }"
                              "]}"),
        .actions =
            {
                {
                    .name         = string_static("Jump"),
                    .bindings     = {{
                            .type                = AssetInputType_Pressed,
                            .key                 = 15,
                            .illegalModifierBits = 0b11,
                    }},
                    .bindingCount = 1,
                },
            },
        .actionCount = 1,
    },
};

static const struct {
  String id;
  String text;
} g_errorTestData[] = {
    {
        .id   = string_static("invalid-json.inputs"),
        .text = string_static("Hello World"),
    },
    {
        .id   = string_static("no-bindings.inputs"),
        .text = string_static("{ \"actions\": [ {"
                              "    \"name\": \"Jump\","
                              "    \"bindings\": []"
                              "     }"
                              "]}"),
    },
    {
        .id   = string_static("duplicate-action-name.inputs"),
        .text = string_static("{ \"actions\": [ {"
                              "    \"name\": \"Test\","
                              "    \"bindings\": [ {"
                              "        \"type\": \"Down\","
                              "        \"key\":  \"Space\""
                              "      } ]"
                              "    }, {"
                              "    \"name\": \"Test\","
                              "    \"bindings\": [ {"
                              "       \"type\": \"Down\","
                              "       \"key\":  \"Space\""
                              "      } ]"
                              "    }"
                              "]}"),
    },
};

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(AssetView) { ecs_access_read(AssetInputMapComp); }

ecs_module_init(loader_inputmap_test_module) {
  ecs_register_view(ManagerView);
  ecs_register_view(AssetView);
}

spec(loader_inputmap) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_allocHeap);
    asset_register(def);
    ecs_register_module(def, loader_inputmap_test_module);

    world  = ecs_world_create(g_allocHeap, def);
    runner = ecs_runner_create(g_allocHeap, world, EcsRunnerFlags_None);
  }

  it("can load inputmaps") {
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

      check_require(ecs_world_has_t(world, asset, AssetLoadedComp));
      const AssetInputMapComp* map = ecs_utils_read_t(world, AssetView, asset, AssetInputMapComp);
      check_require(map->actions.count == g_testData[i].actionCount);
      for (usize a = 0; a != g_testData[i].actionCount; ++a) {
        const AssetInputAction* actualAction   = &map->actions.values[a];
        const TestActionData*   expectedAction = &g_testData[i].actions[a];

        check_eq_int(actualAction->nameHash, string_hash(expectedAction->name));
        check_eq_int(actualAction->blockerBits, expectedAction->blockerBits);
        check_require(actualAction->bindingCount == expectedAction->bindingCount);
        for (usize b = 0; b != expectedAction->bindingCount; ++b) {
          const AssetInputBinding* actualBinding =
              &map->bindings.values[actualAction->bindingIndex + b];
          const AssetInputBinding* expectedBinding = &expectedAction->bindings[b];

          check_eq_int(actualBinding->type, expectedBinding->type);
          check_eq_int(actualBinding->key, expectedBinding->key);
          check_eq_int(actualBinding->requiredModifierBits, expectedBinding->requiredModifierBits);
        }
      }
    }
  }

  it("can unload inputmap assets") {
    const AssetMemRecord record = {.id = string_lit("test.inputs"), .data = g_testData[1].text};
    asset_manager_create_mem(world, AssetManagerFlags_None, &record, 1);
    ecs_world_flush(world);

    EcsEntityId asset;
    {
      AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
      asset                     = asset_lookup(world, manager, string_lit("test.inputs"));
    }
    asset_acquire(world, asset);

    asset_test_wait(runner);
    check(ecs_world_has_t(world, asset, AssetInputMapComp));

    asset_release(world, asset);
    asset_test_wait(runner);
    check(!ecs_world_has_t(world, asset, AssetInputMapComp));
  }

  it("fails when loading invalid inputmap files") {
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
      check(!ecs_world_has_t(world, asset, AssetInputMapComp));
    }
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
