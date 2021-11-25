#include "asset.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_base64.h"
#include "ecs.h"

#include "utils_internal.h"

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(AssetView) { ecs_access_read(AssetMaterialComp); }

ecs_module_init(loader_material_test_module) {
  ecs_register_view(ManagerView);
  ecs_register_view(AssetView);
}

spec(loader_material) {
  EcsDef*        def    = null;
  EcsWorld*      world  = null;
  EcsRunner*     runner = null;
  AssetMemRecord records[4];

  setup() {
    def = ecs_def_create(g_alloc_heap);
    asset_register(def);
    ecs_register_module(def, loader_material_test_module);

    world  = ecs_world_create(g_alloc_heap, def);
    runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_None);

    records[0] = (AssetMemRecord){
        .id   = string_lit("test.spv"),
        .data = string_dup(
            g_alloc_heap,
            base64_decode_scratch(
                string_lit("AwIjBwADAQAIAA0ABgAAAAAAAAARAAIAAQAAAAsABgABAAAAR0xTTC5zdGQuNDUwAAAAAA4"
                           "AAwAAAAAAAQAAAA8ABQAAAAAABAAAAG1haW4AAAAAEwACAAIAAAAhAAMAAwAAAAIAAAA2AA"
                           "UAAgAAAAQAAAAAAAAAAwAAAPgAAgAFAAAA/QABADgAAQA="))),
    };
    records[1] = (AssetMemRecord){
        .id   = string_lit("test_a.ppm"),
        .data = string_lit("P3 1 1 255 1 42 137"),
    };
    records[2] = (AssetMemRecord){
        .id   = string_lit("test_b.ppm"),
        .data = string_lit("P3 1 1 255 1 42 137"),
    };
    records[3] = (AssetMemRecord){
        .id   = string_lit("test.mat"),
        .data = string_lit("{"
                           "  \"shaders\": [{ "
                           "    \"shader\": \"test.spv\","
                           "  }],"
                           "  \"samplers\": ["
                           "    {"
                           "      \"texture\": \"test_a.ppm\","
                           "      \"wrap\": \"Clamp\","
                           "      \"filter\": \"Nearest\","
                           "      \"anisotropy\": \"x4\","
                           "    },"
                           "    {"
                           "      \"texture\": \"test_b.ppm\","
                           "      \"wrap\": \"Repeat\","
                           "      \"filter\": \"Linear\","
                           "      \"anisotropy\": \"None\","
                           "    },"
                           "  ],"
                           "  \"topology\": \"Triangles\","
                           "  \"rasterizer\": \"Fill\","
                           "  \"lineWidth\": 42,"
                           "  \"blend\": \"None\","
                           "  \"depth\": \"Less\","
                           "  \"cull\": \"Back\","
                           "}"),
    };
  }

  it("can load material assets") {
    asset_manager_create_mem(world, records, array_elems(records));
    ecs_world_flush(world);

    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);

    const EcsEntityId asset = asset_lookup(world, manager, string_lit("test.mat"));
    asset_acquire(world, asset);

    asset_test_wait(runner);

    const AssetMaterialComp* mat = ecs_utils_read_t(world, AssetView, asset, AssetMaterialComp);
    check_require(mat->shaderCount == 1);
    check(mat->shaders[0].shader == asset_lookup(world, manager, string_lit("test.spv")));

    check_require(mat->samplerCount == 2);
    check(mat->samplers[0].texture == asset_lookup(world, manager, string_lit("test_a.ppm")));
    check_eq_int(mat->samplers[0].wrap, AssetMaterialWrap_Clamp);
    check_eq_int(mat->samplers[0].filter, AssetMaterialFilter_Nearest);
    check_eq_int(mat->samplers[0].anisotropy, AssetMaterialAniso_x4);

    check(mat->samplers[1].texture == asset_lookup(world, manager, string_lit("test_b.ppm")));
    check_eq_int(mat->samplers[1].wrap, AssetMaterialWrap_Repeat);
    check_eq_int(mat->samplers[1].filter, AssetMaterialFilter_Linear);
    check_eq_int(mat->samplers[1].anisotropy, AssetMaterialAniso_None);

    check_eq_int(mat->topology, AssetMaterialTopology_Triangles);
    check_eq_int(mat->rasterizer, AssetMaterialRasterizer_Fill);
    check_eq_int(mat->lineWidth, 42);
    check_eq_int(mat->blend, AssetMaterialBlend_None);
    check_eq_int(mat->depth, AssetMaterialDepth_Less);
    check_eq_int(mat->cull, AssetMaterialCull_Back);
  }

  it("can unload material assets") {
    asset_manager_create_mem(world, records, array_elems(records));
    ecs_world_flush(world);

    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);

    const EcsEntityId asset = asset_lookup(world, manager, string_lit("test.mat"));

    asset_acquire(world, asset);
    asset_test_wait(runner);

    const AssetMaterialComp* mat    = ecs_utils_read_t(world, AssetView, asset, AssetMaterialComp);
    const EcsEntityId        shader = mat->shaders[0].shader;
    const EcsEntityId        tex1   = mat->samplers[0].texture;
    const EcsEntityId        tex2   = mat->samplers[1].texture;

    check(ecs_world_has_t(world, shader, AssetShaderComp));
    check(ecs_world_has_t(world, tex1, AssetTextureComp));
    check(ecs_world_has_t(world, tex2, AssetTextureComp));

    asset_release(world, asset);
    asset_test_wait(runner);
    check(!ecs_world_has_t(world, asset, AssetMaterialComp));
    check(!ecs_world_has_t(world, shader, AssetShaderComp));
    check(!ecs_world_has_t(world, tex1, AssetTextureComp));
    check(!ecs_world_has_t(world, tex2, AssetTextureComp));
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);

    string_free(g_alloc_heap, records[0].data);
  }
}
