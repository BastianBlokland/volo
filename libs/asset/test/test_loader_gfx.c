#include "asset.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "ecs.h"

#include "utils_internal.h"

static const AssetMemRecord records[] = {
    {
        .id   = string_static("test.gfx"),
        .data = string_static("{"
                              "  \"shaders\": [{ "
                              "    \"shaderId\": \"test.spv\","
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

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(AssetView) { ecs_access_read(AssetGfxComp); }

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
    asset_manager_create_mem(world, records, array_elems(records));
    ecs_world_flush(world);

    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);

    const EcsEntityId asset = asset_lookup(world, manager, string_lit("test.gfx"));
    asset_acquire(world, asset);

    asset_test_wait(runner);

    const AssetGfxComp* gfx = ecs_utils_read_t(world, AssetView, asset, AssetGfxComp);

    check_require(gfx->shaders.count == 1);
    check(gfx->shaders.values[0].shader == asset_lookup(world, manager, string_lit("test.spv")));

    check_require(gfx->samplers.count == 2);
    check(gfx->samplers.values[0].texture == asset_lookup(world, manager, string_lit("a.ppm")));
    check_eq_int(gfx->samplers.values[0].wrap, AssetGfxWrap_Clamp);
    check_eq_int(gfx->samplers.values[0].filter, AssetGfxFilter_Nearest);
    check_eq_int(gfx->samplers.values[0].anisotropy, AssetGfxAniso_x4);

    check(gfx->samplers.values[1].texture == asset_lookup(world, manager, string_lit("b.ppm")));
    check_eq_int(gfx->samplers.values[1].wrap, AssetGfxWrap_Repeat);
    check_eq_int(gfx->samplers.values[1].filter, AssetGfxFilter_Linear);
    check_eq_int(gfx->samplers.values[1].anisotropy, AssetGfxAniso_None);

    check(gfx->mesh == asset_lookup(world, manager, string_lit("a.obj")));
    check_eq_int(gfx->topology, AssetGfxTopology_Triangles);
    check_eq_int(gfx->rasterizer, AssetGfxRasterizer_Fill);
    check_eq_int(gfx->lineWidth, 42);
    check_eq_int(gfx->blend, AssetGfxBlend_None);
    check_eq_int(gfx->depth, AssetGfxDepth_Less);
    check_eq_int(gfx->cull, AssetGfxCull_Back);
  }

  it("can unload graphic assets") {
    asset_manager_create_mem(world, records, array_elems(records));
    ecs_world_flush(world);

    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
    const EcsEntityId asset   = asset_lookup(world, manager, string_lit("test.gfx"));

    asset_acquire(world, asset);
    asset_test_wait(runner);

    check(ecs_world_has_t(world, asset, AssetGfxComp));

    asset_release(world, asset);
    asset_test_wait(runner);

    check(!ecs_world_has_t(world, asset, AssetGfxComp));
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
