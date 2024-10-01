#include "asset.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_base64.h"
#include "ecs.h"

#include "utils_internal.h"

/**
 * The test images are exported from gimp 2.10.20 and then base64 encoded.
 */

static const struct {
  String   id;
  String   base64Data;
  GeoColor pixels[16];
  usize    pixelCount;
} g_testData[] = {
    {
        .id         = string_static("2x2_rgba.png"),
        .base64Data = string_static("iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAYAAABytg0kAAAAGUlEQVQI1wXBAQ"
                                    "0AAAzDIJbcv+UeRNJNwgM+/wYAegsO9AAAAABJRU5ErkJggg=="),
        .pixels =
            {
                {0.0f, 0.0f, 1.0f, 1.0f},
                {1.0f, 1.0f, 1.0f, 1.0f},
                {1.0f, 0.0f, 0.0f, 1.0f},
                {0.0f, 1.0f, 0.0f, 1.0f},
            },
        .pixelCount = 4,
    },
};

static const struct {
  String id;
  String text;
} g_errorTestData[] = {
    {
        .id   = string_static("invalid.png"),
        .text = string_static("Hello World"),
    },
};

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(AssetView) { ecs_access_read(AssetTextureComp); }

ecs_module_init(loader_texture_png_test_module) {
  ecs_register_view(ManagerView);
  ecs_register_view(AssetView);
}

spec(loader_texture_png) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_allocHeap);
    asset_register(def);
    ecs_register_module(def, loader_texture_png_test_module);

    world  = ecs_world_create(g_allocHeap, def);
    runner = ecs_runner_create(g_allocHeap, world, EcsRunnerFlags_None);
  }

  it("can load png images") {
    AssetMemRecord records[array_elems(g_testData)];
    for (usize i = 0; i != array_elems(g_testData); ++i) {
      records[i] = (AssetMemRecord){
          .id   = g_testData[i].id,
          .data = string_dup(g_allocHeap, base64_decode_scratch(g_testData[i].base64Data)),
      };
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
      const AssetTextureComp* tex = ecs_utils_read_t(world, AssetView, asset, AssetTextureComp);
      check_eq_int(tex->format, AssetTextureFormat_u8_rgba);
      check_require(tex->height * tex->height == g_testData[i].pixelCount);
      for (usize p = 0; p != g_testData[i].pixelCount; ++p) {
        const GeoColor colorSrgb = geo_color_linear_to_srgb(asset_texture_at(tex, 0, p));
        check_eq_float(colorSrgb.r, g_testData[i].pixels[p].r, 1e-2);
        check_eq_float(colorSrgb.g, g_testData[i].pixels[p].g, 1e-2);
        check_eq_float(colorSrgb.b, g_testData[i].pixels[p].b, 1e-2);
        check_eq_float(colorSrgb.a, g_testData[i].pixels[p].a, 1e-2);
      }
    };

    array_for_t(records, AssetMemRecord, rec) { string_free(g_allocHeap, rec->data); }
  }

  it("can unload png texture assets") {
    const AssetMemRecord record = {
        .id   = string_lit("tex.png"),
        .data = string_dup(g_allocHeap, base64_decode_scratch(g_testData[0].base64Data)),
    };
    asset_manager_create_mem(world, AssetManagerFlags_None, &record, 1);
    ecs_world_flush(world);

    EcsEntityId asset;
    {
      AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
      asset                     = asset_lookup(world, manager, string_lit("tex.png"));
    }
    asset_acquire(world, asset);

    asset_test_wait(runner);
    check(ecs_world_has_t(world, asset, AssetTextureComp));

    asset_release(world, asset);
    asset_test_wait(runner);
    check(!ecs_world_has_t(world, asset, AssetTextureComp));

    string_free(g_allocHeap, record.data);
  }

  it("fails when loading invalid png files") {
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
      check(!ecs_world_has_t(world, asset, AssetTextureComp));
    }
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
