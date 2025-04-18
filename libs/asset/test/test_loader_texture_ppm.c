#include "asset_manager.h"
#include "asset_register.h"
#include "asset_texture.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "ecs_utils.h"
#include "ecs_world.h"

#include "utils_internal.h"

static const struct {
  String   id;
  String   text;
  GeoColor pixels[16];
  usize    pixelCount;
} g_testData[] = {
    {
        .id   = string_static("p3_formatted_lossless.ppm"),
        .text = string_static("P3\n"
                              "2 2 255\n"
                              "255 0 0\n"
                              "0 255 0\n"
                              "0 0 255\n"
                              "128 128 128\n"),
        .pixels =
            {
                {0.0f, 0.0f, 1.0f, 1.0f},
                {0.5f, 0.5f, 0.5f, 1.0f},
                {1.0f, 0.0f, 0.0f, 1.0f},
                {0.0f, 1.0f, 0.0f, 1.0f},
            },
        .pixelCount = 4,
    },
    {
        .id   = string_static("p3_color_per_line_lossless.ppm"),
        .text = string_static("P3\n"
                              "2\n2\n255\n"
                              "255\n0\n0\n"
                              "0\n255\n0\n"
                              "0\n0\n255\n"
                              "128\n128\n128\n"),
        .pixels =
            {
                {0.0f, 0.0f, 1.0f, 1.0f},
                {0.5f, 0.5f, 0.5f, 1.0f},
                {1.0f, 0.0f, 0.0f, 1.0f},
                {0.0f, 1.0f, 0.0f, 1.0f},
            },
        .pixelCount = 4,
    },
    {
        .id   = string_static("p3_single_line_lossless.ppm"),
        .text = string_static("P3 2 2 255 255 0 0 0 255 0 0 0 255 128 128 128"),
        .pixels =
            {
                {0.0f, 0.0f, 1.0f, 1.0f},
                {0.5f, 0.5f, 0.5f, 1.0f},
                {1.0f, 0.0f, 0.0f, 1.0f},
                {0.0f, 1.0f, 0.0f, 1.0f},
            },
        .pixelCount = 4,
    },
    {
        .id   = string_static("p3_commented_lossless.ppm"),
        .text = string_static("# Hello\nP3\n"
                              "# Comments\n2# Are\n2#Supported\n255#Everywhere\n"
                              "# In\n255# The\n0   # Format\n0 # Will\n"
                              "# That\n0 # Parse\n255 # Correctly?\n0\n"
                              "0 0 255\n"
                              "128 128 128# End of file\n"),
        .pixels =
            {
                {0.0f, 0.0f, 1.0f, 1.0f},
                {0.5f, 0.5f, 0.5f, 1.0f},
                {1.0f, 0.0f, 0.0f, 1.0f},
                {0.0f, 1.0f, 0.0f, 1.0f},
            },
        .pixelCount = 4,
    },
    {
        .id   = string_static("p3_windows_line-endings_lossless.ppm"),
        .text = string_static("P3\r\n"
                              "2 2 255\r\n"
                              "# Comments with windows line-endings\r\n"
                              "255 0 0\r\n"
                              "0 255 0\r\n"
                              "0 0 255\r\n"
                              "128 128 128\r\n"),
        .pixels =
            {
                {0.0f, 0.0f, 1.0f, 1.0f},
                {0.5f, 0.5f, 0.5f, 1.0f},
                {1.0f, 0.0f, 0.0f, 1.0f},
                {0.0f, 1.0f, 0.0f, 1.0f},
            },
        .pixelCount = 4,
    },
    {
        .id   = string_static("p3_unspecified_colors_lossless.ppm"),
        .text = string_static("P3 2 2 255\n"
                              "255 0 0\n"
                              "0 255 0"),
        .pixels =
            {
                {0.0f, 0.0f, 0.0f, 1.0f},
                {0.0f, 0.0f, 0.0f, 1.0f},
                {1.0f, 0.0f, 0.0f, 1.0f},
                {0.0f, 1.0f, 0.0f, 1.0f},
            },
        .pixelCount = 4,
    },
    {
        .id   = string_static("p3_extra_colors_lossless.ppm"),
        .text = string_static("P3 1 1 255\n"
                              "255 0 0\n"
                              "0 255 0\n"
                              "0 0 255"),
        .pixels =
            {
                {1.0f, 0.0f, 0.0f, 1.0f},
            },
        .pixelCount = 1,
    },
    {
        .id   = string_static("p6_lossless.ppm"),
        .text = string_static("P6 2 2 255\n"
                              "\xFF\x0\x0"
                              "\x0\xFF\x0"
                              "\x0\x0\xFF"
                              "\x80\x80\x80"),
        .pixels =
            {
                {0.0f, 0.0f, 1.0f, 1.0f},
                {0.5f, 0.5f, 0.5f, 1.0f},
                {1.0f, 0.0f, 0.0f, 1.0f},
                {0.0f, 1.0f, 0.0f, 1.0f},
            },
        .pixelCount = 4,
    },
    {
        .id   = string_static("p6_unspecified_colors_lossless.ppm"),
        .text = string_static("P6 2 2 255\n"
                              "\xFF\x1\x1"
                              "\x1\xFF\x1"
                              "\x1\x1\x1"
                              "\x1\x1\x1"),
        .pixels =
            {
                {0.004f, 0.004f, 0.004f, 1.0f},
                {0.004f, 0.004f, 0.004f, 1.0f},
                {1.0f, 0.004f, 0.004f, 1.0f},
                {0.004f, 1.0f, 0.004f, 1.0f},
            },
        .pixelCount = 4,
    },
    {
        .id   = string_static("p6_extra_colors_lossless.ppm"),
        .text = string_static("P6 1 1 255\n"
                              "\xFF\x1\x1"
                              "\x1\xFF\x1"
                              "\x1\x1\xFF"),
        .pixels =
            {
                {1.0f, 0.004f, 0.004f, 1.0f},
            },
        .pixelCount = 1,
    },
};

static const struct {
  String id;
  String text;
} g_errorTestData[] = {
    {
        .id   = string_static("invalid-format-type.ppm"),
        .text = string_static("P9 1 1 255 255 255 255"),
    },
    {
        .id   = string_static("invalid-size.ppm"),
        .text = string_static("P3 0 0 255 255 255 255"),
    },
    {
        .id   = string_static("invalid-bitdepth.ppm"),
        .text = string_static("P3 0 0 128 128 128 128"),
    },
};

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(AssetView) { ecs_access_read(AssetTextureComp); }

ecs_module_init(loader_texture_ppm_test_module) {
  ecs_register_view(ManagerView);
  ecs_register_view(AssetView);
}

spec(loader_texture_ppm) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_allocHeap);
    asset_register(def);
    ecs_register_module(def, loader_texture_ppm_test_module);

    world  = ecs_world_create(g_allocHeap, def);
    runner = ecs_runner_create(g_allocHeap, world, EcsRunnerFlags_None);
  }

  it("can load ppm images") {
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
  }

  it("can unload ppm texture assets") {
    const AssetMemRecord record = {.id = string_lit("tex.ppm"), .data = g_testData[0].text};
    asset_manager_create_mem(world, AssetManagerFlags_None, &record, 1);
    ecs_world_flush(world);

    EcsEntityId asset;
    {
      AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
      asset                     = asset_lookup(world, manager, string_lit("tex.ppm"));
    }
    asset_acquire(world, asset);

    asset_test_wait(runner);
    check(ecs_world_has_t(world, asset, AssetTextureComp));

    asset_release(world, asset);
    asset_test_wait(runner);
    check(!ecs_world_has_t(world, asset, AssetTextureComp));
  }

  it("fails when loading invalid ppm files") {
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
