#include "asset.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "ecs.h"

#include "utils_internal.h"

static const AssetMemRecord g_testData[] = {
    {
        .id   = string_static("test.gra"),
        .data = string_static("{"
                              "  \"shaders\": [{ "
                              "    \"shaderId\": \"test.spv\","
                              "    \"overrides\": [{ "
                              "      \"name\": \"Test\","
                              "      \"binding\": 42,"
                              "      \"value\": 1337.1337"
                              "    }],"
                              "  }],"
                              "  \"samplers\": ["
                              "    {"
                              "      \"textureId\": \"a.ppm\","
                              "      \"wrap\": \"Clamp\","
                              "      \"filter\": \"Nearest\","
                              "      \"anisotropy\": \"x4\","
                              "    },"
                              "    {"
                              "      \"textureId\": \"b.ppm\","
                              "      \"wrap\": \"Repeat\","
                              "      \"filter\": \"Linear\","
                              "      \"anisotropy\": \"None\","
                              "    },"
                              "  ],"
                              "  \"meshId\": \"a.obj\","
                              "  \"topology\": \"Triangles\","
                              "  \"rasterizer\": \"Fill\","
                              "  \"lineWidth\": 42,"
                              "  \"blend\": \"None\","
                              "  \"depth\": \"Less\","
                              "  \"cull\": \"Back\","
                              "}"),
    },
};

static const struct {
  String id;
  String text;
} g_errorTestData[] = {
    {
        .id   = string_static("missing_mesh.gra"),
        .text = string_static("{"
                              "  \"shaders\": [],"
                              "  \"samplers\": [],"
                              "  \"topology\": \"Triangles\","
                              "  \"rasterizer\": \"Fill\","
                              "  \"lineWidth\": 42,"
                              "  \"blend\": \"None\","
                              "  \"depth\": \"Less\","
                              "  \"cull\": \"Back\","
                              "}"),
    },
    {
        .id   = string_static("empty_mesh.gra"),
        .text = string_static("{"
                              "  \"shaders\": [],"
                              "  \"samplers\": [],"
                              "  \"meshId\": \"\","
                              "  \"topology\": \"Triangles\","
                              "  \"rasterizer\": \"Fill\","
                              "  \"lineWidth\": 42,"
                              "  \"blend\": \"None\","
                              "  \"depth\": \"Less\","
                              "  \"cull\": \"Back\","
                              "}"),
    },
};

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(AssetView) { ecs_access_read(AssetGraphicComp); }

ecs_module_init(loader_graphic_test_module) {
  ecs_register_view(ManagerView);
  ecs_register_view(AssetView);
}

spec(loader_graphic) {
  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    asset_register(def);
    ecs_register_module(def, loader_graphic_test_module);

    world  = ecs_world_create(g_alloc_heap, def);
    runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_None);
  }

  it("can load graphic assets") {
    asset_manager_create_mem(world, AssetManagerFlags_None, g_testData, array_elems(g_testData));
    ecs_world_flush(world);

    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);

    const EcsEntityId asset = asset_lookup(world, manager, string_lit("test.gra"));
    asset_acquire(world, asset);

    asset_test_wait(runner);

    const AssetGraphicComp* graphic = ecs_utils_read_t(world, AssetView, asset, AssetGraphicComp);

    check_require(graphic->shaders.count == 1);
    check(
        graphic->shaders.values[0].shader == asset_lookup(world, manager, string_lit("test.spv")));
    check_require(graphic->shaders.values[0].overrides.count == 1);
    check_eq_string(graphic->shaders.values[0].overrides.values[0].name, string_lit("Test"));
    check_eq_int(graphic->shaders.values[0].overrides.values[0].binding, 42);
    check_eq_float(graphic->shaders.values[0].overrides.values[0].value, 1337.1337, 1e-8);

    check_require(graphic->samplers.count == 2);
    check(graphic->samplers.values[0].texture == asset_lookup(world, manager, string_lit("a.ppm")));
    check_eq_int(graphic->samplers.values[0].wrap, AssetGraphicWrap_Clamp);
    check_eq_int(graphic->samplers.values[0].filter, AssetGraphicFilter_Nearest);
    check_eq_int(graphic->samplers.values[0].anisotropy, AssetGraphicAniso_x4);

    check(graphic->samplers.values[1].texture == asset_lookup(world, manager, string_lit("b.ppm")));
    check_eq_int(graphic->samplers.values[1].wrap, AssetGraphicWrap_Repeat);
    check_eq_int(graphic->samplers.values[1].filter, AssetGraphicFilter_Linear);
    check_eq_int(graphic->samplers.values[1].anisotropy, AssetGraphicAniso_None);

    check(graphic->mesh == asset_lookup(world, manager, string_lit("a.obj")));
    check_eq_int(graphic->topology, AssetGraphicTopology_Triangles);
    check_eq_int(graphic->rasterizer, AssetGraphicRasterizer_Fill);
    check_eq_int(graphic->lineWidth, 42);
    check_eq_int(graphic->blend, AssetGraphicBlend_None);
    check_eq_int(graphic->depth, AssetGraphicDepth_Less);
    check_eq_int(graphic->cull, AssetGraphicCull_Back);
  }

  it("can unload graphic assets") {
    asset_manager_create_mem(world, AssetManagerFlags_None, g_testData, array_elems(g_testData));
    ecs_world_flush(world);

    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
    const EcsEntityId asset   = asset_lookup(world, manager, string_lit("test.gra"));

    asset_acquire(world, asset);
    asset_test_wait(runner);

    check(ecs_world_has_t(world, asset, AssetGraphicComp));

    asset_release(world, asset);
    asset_test_wait(runner);

    check(!ecs_world_has_t(world, asset, AssetGraphicComp));
  }

  it("fails when loading invalid graphic files") {
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
      check(!ecs_world_has_t(world, asset, AssetGraphicComp));
    }
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
