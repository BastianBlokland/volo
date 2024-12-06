#include "asset_manager.h"
#include "asset_register.h"
#include "asset_weapon.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "ecs_utils.h"
#include "ecs_world.h"

#include "utils_internal.h"

typedef struct {
  String       name;
  TimeDuration intervalMin, intervalMax;
} TestWeaponData;

static const struct {
  String         id;
  String         text;
  TestWeaponData weapons[2];
  usize          weaponCount;
} g_testData[] = {
    {
        .id          = string_static("empty.weapons"),
        .text        = string_static("{\"weapons\": []}"),
        .weapons     = {0},
        .weaponCount = 0,
    },
    {
        .id   = string_static("single.weapons"),
        .text = string_static("{ \"weapons\": [ {"
                              "      \"name\": \"Pistol\","
                              "      \"intervalMin\": 1,"
                              "      \"intervalMax\": 2,"
                              "      \"readySpeed\": 3.5,"
                              "      \"readyMinTime\": 3,"
                              "      \"effects\": []"
                              "  }"
                              "]}"),
        .weapons =
            {
                {
                    .name        = string_static("Pistol"),
                    .intervalMin = time_seconds(1),
                    .intervalMax = time_seconds(2),
                },
            },
        .weaponCount = 1,
    },
    {
        .id   = string_static("multi.weapons"),
        .text = string_static("{ \"weapons\": [ {"
                              "      \"name\": \"Pistol\","
                              "      \"intervalMin\": 1,"
                              "      \"intervalMax\": 2,"
                              "      \"readySpeed\": 3.5,"
                              "      \"readyMinTime\": 3,"
                              "      \"effects\": []"
                              "  }, {"
                              "      \"name\": \"Sword\","
                              "      \"intervalMin\": 2,"
                              "      \"intervalMax\": 3,"
                              "      \"readySpeed\": 3.5,"
                              "      \"readyMinTime\": 3,"
                              "      \"effects\": []"
                              "  }"
                              "]}"),
        .weapons =
            {
                {
                    .name        = string_static("Pistol"),
                    .intervalMin = time_seconds(1),
                    .intervalMax = time_seconds(2),
                },
                {
                    .name        = string_static("Sword"),
                    .intervalMin = time_seconds(2),
                    .intervalMax = time_seconds(3),
                },
            },
        .weaponCount = 2,
    },
    {
        .id          = string_static("effect-vfx.weapons"),
        .text        = string_static("{ \"weapons\": [ {"
                                     "      \"name\": \"Pistol\","
                                     "      \"intervalMin\": 1,"
                                     "      \"intervalMax\": 2,"
                                     "      \"readySpeed\": 3.5,"
                                     "      \"readyMinTime\": 3,"
                                     "      \"effects\": [ {"
                                     "        \"$type\": \"AssetWeaponEffect_Vfx\","
                                     "        \"assetId\": \"test1\","
                                     "        \"originJoint\": \"test2\","
                                     "        \"delay\": 0,"
                                     "        \"duration\": 1"
                                     "      }]"
                                     "  }"
                                     "]}"),
        .weapons     = {{
                .name        = string_static("Pistol"),
                .intervalMin = time_seconds(1),
                .intervalMax = time_seconds(2),
        }},
        .weaponCount = 1,
    },
};

static const struct {
  String id;
  String text;
} g_errorTestData[] = {
    {
        .id   = string_static("invalid-json.weapons"),
        .text = string_static("Hello World"),
    },
    {
        .id   = string_static("duplicate-weapon-name.weapons"),
        .text = string_static("{ \"weapons\": [ {"
                              "      \"name\": \"Pistol\","
                              "      \"intervalMin\": 1,"
                              "      \"intervalMax\": 2,"
                              "      \"readySpeed\": 3.5,"
                              "      \"readyMinTime\": 3,"
                              "      \"effects\": []"
                              "    }, {"
                              "      \"name\": \"Pistol\","
                              "      \"intervalMin\": 1,"
                              "      \"intervalMax\": 2,"
                              "      \"readySpeed\": 3.5,"
                              "      \"readyMinTime\": 3,"
                              "      \"effects\": []"
                              "    }"
                              "]}"),
    },
};

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(AssetView) { ecs_access_read(AssetWeaponMapComp); }

ecs_module_init(loader_weapon_test_module) {
  ecs_register_view(ManagerView);
  ecs_register_view(AssetView);
}

spec(loader_weapon) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_allocHeap);
    asset_register(def);
    ecs_register_module(def, loader_weapon_test_module);

    world  = ecs_world_create(g_allocHeap, def);
    runner = ecs_runner_create(g_allocHeap, world, EcsRunnerFlags_None);
  }

  it("can load weapon maps") {
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

      const AssetWeaponMapComp* map = ecs_utils_read_t(world, AssetView, asset, AssetWeaponMapComp);
      check_require(map->weapons.count == g_testData[i].weaponCount);
      for (usize a = 0; a != g_testData[i].weaponCount; ++a) {
        const AssetWeapon*    actualWeapon   = &map->weapons.values[a];
        const TestWeaponData* expectedWeapon = &g_testData[i].weapons[a];

        check_eq_int(actualWeapon->nameHash, string_hash(expectedWeapon->name));
        check_eq_int(actualWeapon->intervalMin, expectedWeapon->intervalMin);
        check_eq_int(actualWeapon->intervalMax, expectedWeapon->intervalMax);
      }
    }
  }

  it("can unload weapon-map assets") {
    const AssetMemRecord record = {.id = string_lit("test.weapons"), .data = g_testData[1].text};
    asset_manager_create_mem(world, AssetManagerFlags_None, &record, 1);
    ecs_world_flush(world);

    EcsEntityId asset;
    {
      AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
      asset                     = asset_lookup(world, manager, string_lit("test.weapons"));
    }
    asset_acquire(world, asset);

    asset_test_wait(runner);
    check(ecs_world_has_t(world, asset, AssetWeaponMapComp));

    asset_release(world, asset);
    asset_test_wait(runner);
    check(!ecs_world_has_t(world, asset, AssetWeaponMapComp));
  }

  it("fails when loading invalid weapon map files") {
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
      check(!ecs_world_has_t(world, asset, AssetWeaponMapComp));
    }
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
