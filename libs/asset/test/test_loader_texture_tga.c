#include "asset.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_base64.h"
#include "ecs.h"

#include "utils_internal.h"

/**
 * The test images are exported from gimp 2.10.20 and then base64 encoded.
 */

static const struct {
  String            id;
  String            base64Data;
  AssetTexturePixel pixels[16];
  usize             pixelCount;
} g_testData[] = {
    {
        .id         = string_static("2x2_upper-left_uncompressed.tga"),
        .base64Data = string_static(
            "AAACAAAAAAAAAAIAAgACABggAAD/AP8A/wAA////AAAAAAAAAABUUlVFVklTSU9OLVhGSUxFLgA="),
        .pixels =
            {
                {255, 0, 0, 255},
                {0, 255, 0, 255},
                {0, 0, 255, 255},
                {255, 255, 255, 255},
            },
        .pixelCount = 4,
    },
    {
        .id         = string_static("2x2_bottom-left_uncompressed.tga"),
        .base64Data = string_static(
            "AAACAAAAAAAAAAAAAgACABgA/wAA////AAD/AP8AAAAAAAAAAABUUlVFVklTSU9OLVhGSUxFLgA="),
        .pixels =
            {
                {255, 0, 0, 255},
                {0, 255, 0, 255},
                {0, 0, 255, 255},
                {255, 255, 255, 255},
            },
        .pixelCount = 4,
    },
    {
        .id         = string_static("2x2_upper-left_uncompressed_alpha.tga"),
        .base64Data = string_static(
            "AAACAAAAAAAAAAIAAgACACAoAAD//wD/AJP/AACT/////wAAAAAAAAAAVFJVRVZJU0lPTi1YRklMRS4A"),
        .pixels =
            {
                {255, 0, 0, 255},
                {0, 255, 0, 147},
                {0, 0, 255, 147},
                {255, 255, 255, 255},
            },
        .pixelCount = 4,
    },
    {
        .id         = string_static("2x2_bottom-left_uncompressed_alpha.tga"),
        .base64Data = string_static(
            "AAACAAAAAAAAAAAAAgACACAI/wAAk/////8AAP//AP8AkwAAAAAAAAAAVFJVRVZJU0lPTi1YRklMRS4A"),
        .pixels =
            {
                {255, 0, 0, 255},
                {0, 255, 0, 147},
                {0, 0, 255, 147},
                {255, 255, 255, 255},
            },
        .pixelCount = 4,
    },
    {
        .id         = string_static("4x4_upper-left_rle-compressed.tga"),
        .base64Data = string_static("AAAKAAAAAAAAAAQABAAEABggggAA/wAA/wCDAP8AAwD/AP8AAAAA/wD/AIH///"
                                    "+BAAAAAAAAAAAAAABUUlVFVklTSU9OLVhGSUxFLgA="),
        .pixels =
            {
                {255, 0, 0, 255},
                {255, 0, 0, 255},
                {255, 0, 0, 255},
                {0, 255, 0, 255},
                {0, 255, 0, 255},
                {0, 255, 0, 255},
                {0, 255, 0, 255},
                {0, 255, 0, 255},
                {0, 255, 0, 255},
                {0, 0, 255, 255},
                {255, 0, 0, 255},
                {0, 255, 0, 255},
                {255, 255, 255, 255},
                {255, 255, 255, 255},
                {0, 0, 0, 255},
                {0, 0, 0, 255},
            },
        .pixelCount = 16,
    },
    {
        .id         = string_static("4x4_bottom-left_rle-compressed.tga"),
        .base64Data = string_static("AAAKAAAAAAAAAAAABAAEABgAgf///4EAAAADAP8A/wAAAAD/AP8AgwD/"
                                    "AIIAAP8AAP8AAAAAAAAAAABUUlVFVklTSU9OLVhGSUxFLgA="),
        .pixels =
            {
                {255, 0, 0, 255},
                {255, 0, 0, 255},
                {255, 0, 0, 255},
                {0, 255, 0, 255},
                {0, 255, 0, 255},
                {0, 255, 0, 255},
                {0, 255, 0, 255},
                {0, 255, 0, 255},
                {0, 255, 0, 255},
                {0, 0, 255, 255},
                {255, 0, 0, 255},
                {0, 255, 0, 255},
                {255, 255, 255, 255},
                {255, 255, 255, 255},
                {0, 0, 0, 255},
                {0, 0, 0, 255},
            },
        .pixelCount = 16,
    },
    {
        .id         = string_static("4x4_upper-left_rle-compressed_alpha.tga"),
        .base64Data = string_static(
            "AAAKAAAAAAAAAAAABAAEACAIA/////////+oAAAA/wAAAJMDAP8Ak/8AAP8AAP+TAP8A/wMA/wD/AP8AkwD/"
            "AP8A/wCTAwAA/5MAAP//AAD/kwD/AP8AAAAAAAAAAFRSVUVWSVNJT04tWEZJTEUuAA=="),
        .pixels =
            {
                {255, 0, 0, 147},
                {255, 0, 0, 255},
                {255, 0, 0, 147},
                {0, 255, 0, 255},
                {0, 255, 0, 255},
                {0, 255, 0, 147},
                {0, 255, 0, 255},
                {0, 255, 0, 147},
                {0, 255, 0, 147},
                {0, 0, 255, 255},
                {255, 0, 0, 147},
                {0, 255, 0, 255},
                {255, 255, 255, 255},
                {255, 255, 255, 168},
                {0, 0, 0, 255},
                {0, 0, 0, 147},
            },
        .pixelCount = 16,
    },
};

static const struct {
  String id;
  String text;
} g_errorTestData[] = {
    {
        .id   = string_static("invalid.tga"),
        .text = string_static("Hello World"),
    },
};

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(AssetView) { ecs_access_read(AssetTextureComp); }

ecs_module_init(loader_texture_tga_test_module) {
  ecs_register_view(ManagerView);
  ecs_register_view(AssetView);
}

spec(loader_texture_tga) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    asset_register(def);
    ecs_register_module(def, loader_texture_tga_test_module);

    world  = ecs_world_create(g_alloc_heap, def);
    runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_None);
  }

  it("can load tga images") {
    AssetMemRecord records[array_elems(g_testData)];
    for (usize i = 0; i != array_elems(g_testData); ++i) {
      records[i] = (AssetMemRecord){
          .id   = g_testData[i].id,
          .data = string_dup(g_alloc_heap, base64_decode_scratch(g_testData[i].base64Data)),
      };
    }
    asset_manager_create_mem(world, AssetManagerFlags_None, records, array_elems(g_testData));
    ecs_world_flush(world);

    for (usize i = 0; i != array_elems(g_testData); ++i) {
      AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
      const EcsEntityId asset   = asset_lookup(world, manager, records[i].id);
      asset_acquire(world, asset);

      asset_test_wait(runner);

      check_require(ecs_world_has_t(world, asset, AssetLoadedComp));
      const AssetTextureComp* tex = ecs_utils_read_t(world, AssetView, asset, AssetTextureComp);
      check_require(tex->height * tex->height == g_testData[i].pixelCount);
      for (usize p = 0; p != g_testData[i].pixelCount; ++p) {
        check_eq_int(tex->pixels[p].r, g_testData[i].pixels[p].r);
        check_eq_int(tex->pixels[p].g, g_testData[i].pixels[p].g);
        check_eq_int(tex->pixels[p].b, g_testData[i].pixels[p].b);
        check_eq_int(tex->pixels[p].a, g_testData[i].pixels[p].a);
      }
    };

    array_for_t(records, AssetMemRecord, rec) { string_free(g_alloc_heap, rec->data); }
  }

  it("can unload tga texture assets") {
    const AssetMemRecord record = {
        .id   = string_lit("tex.tga"),
        .data = string_dup(g_alloc_heap, base64_decode_scratch(g_testData[0].base64Data)),
    };
    asset_manager_create_mem(world, AssetManagerFlags_None, &record, 1);
    ecs_world_flush(world);

    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
    const EcsEntityId asset   = asset_lookup(world, manager, string_lit("tex.tga"));

    asset_acquire(world, asset);
    asset_test_wait(runner);
    check(ecs_world_has_t(world, asset, AssetTextureComp));

    asset_release(world, asset);
    asset_test_wait(runner);
    check(!ecs_world_has_t(world, asset, AssetTextureComp));

    string_free(g_alloc_heap, record.data);
  }

  it("fails when loading invalid tga files") {
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
      check(!ecs_world_has_t(world, asset, AssetTextureComp));
    }
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
