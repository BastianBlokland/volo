#include "asset.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "ecs.h"

#include "utils_internal.h"

static const AssetMemRecord g_testData[] = {
    {
        .id   = string_static("one.ptx"),
        .data = string_static("{"
                              "  \"type\": \"One\","
                              "  \"channels\": 1,"
                              "  \"size\": 1,"
                              "  \"frequency\": 1,"
                              "  \"power\": 1,"
                              "  \"seed\": 1"
                              "}"),
    },
    {
        .id   = string_static("test.atl"),
        .data = string_static("{"
                              "  \"size\": 64,"
                              "  \"entrySize\": 32,"
                              "  \"mipmaps\": true,"
                              "  \"srgb\": true,"
                              "  \"entries\": ["
                              "    { \"name\": \"a\", \"texture\": \"one.ptx\"},"
                              "    { \"name\": \"b\", \"texture\": \"one.ptx\"}"
                              "  ]"
                              "}"),
    },
};

static const AssetMemRecord g_errorTestData[] = {
    {
        .id   = string_static("no-entries.atl"),
        .data = string_static("{"
                              "  \"size\": 64,"
                              "  \"entrySize\": 32,"
                              "  \"mipmaps\": true,"
                              "  \"srgb\": true,"
                              "  \"entries\": []"
                              "}"),
    },
};

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(AssetView) {
  ecs_access_read(AssetAtlasComp);
  ecs_access_read(AssetTextureComp);
}

ecs_module_init(loader_atlas_test_module) {
  ecs_register_view(ManagerView);
  ecs_register_view(AssetView);
}

spec(loader_texture_atlas) {
  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    asset_register(def);
    ecs_register_module(def, loader_atlas_test_module);

    world  = ecs_world_create(g_alloc_heap, def);
    runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_None);
  }

  it("can load atlas assets") {
    asset_manager_create_mem(world, AssetManagerFlags_None, g_testData, array_elems(g_testData));
    ecs_world_flush(world);

    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);

    const EcsEntityId asset = asset_lookup(world, manager, string_lit("test.atl"));
    asset_acquire(world, asset);

    asset_test_wait(runner);

    check_require(ecs_world_has_t(world, asset, AssetLoadedComp));
    const AssetAtlasComp*   atlas = ecs_utils_read_t(world, AssetView, asset, AssetAtlasComp);
    const AssetTextureComp* tex   = ecs_utils_read_t(world, AssetView, asset, AssetTextureComp);

    check_require(atlas->entryCount == 2);
    check_eq_int(asset_atlas_lookup(atlas, string_hash_lit("a"))->atlasIndex, 0);
    check_eq_int(asset_atlas_lookup(atlas, string_hash_lit("b"))->atlasIndex, 1);
    check(!asset_atlas_lookup(atlas, string_hash_lit("c")));

    check_eq_int(tex->type, AssetTextureType_Byte);
    check_eq_int(tex->channels, 4);
    check_eq_int(tex->width, 64);
    check_eq_int(tex->height, 64);
  }

  it("can unload atlas assets") {
    asset_manager_create_mem(world, AssetManagerFlags_None, g_testData, array_elems(g_testData));
    ecs_world_flush(world);

    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
    const EcsEntityId asset   = asset_lookup(world, manager, string_lit("test.atl"));

    asset_acquire(world, asset);
    asset_test_wait(runner);

    check(ecs_world_has_t(world, asset, AssetAtlasComp));
    check(ecs_world_has_t(world, asset, AssetTextureComp));

    asset_release(world, asset);
    asset_test_wait(runner);

    check(!ecs_world_has_t(world, asset, AssetAtlasComp));
    check(!ecs_world_has_t(world, asset, AssetTextureComp));
  }

  it("fails when loading invalid atlas files") {
    asset_manager_create_mem(world, AssetManagerFlags_None, g_testData, array_elems(g_testData));
    ecs_world_flush(world);

    array_for_t(g_errorTestData, AssetMemRecord, errRec) {
      AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
      const EcsEntityId asset   = asset_lookup(world, manager, errRec->id);
      asset_acquire(world, asset);
      asset_test_wait(runner);

      check(ecs_world_has_t(world, asset, AssetFailedComp));
      check(!ecs_world_has_t(world, asset, AssetAtlasComp));
      check(!ecs_world_has_t(world, asset, AssetTextureComp));
    }
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
