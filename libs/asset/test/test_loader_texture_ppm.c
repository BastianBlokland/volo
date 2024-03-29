#include "asset.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "ecs.h"

#include "utils_internal.h"

static const struct {
  String              id;
  String              text;
  AssetTexturePixelB4 pixels[16];
  usize               pixelCount;
} g_testData[] = {
    {
        .id   = string_static("p3_formatted.ppm"),
        .text = string_static("P3\n"
                              "2 2 255\n"
                              "255 0 0\n"
                              "0 255 0\n"
                              "0 0 255\n"
                              "128 128 128\n"),
        .pixels =
            {
                {0, 0, 255, 255},
                {128, 128, 128, 255},
                {255, 0, 0, 255},
                {0, 255, 0, 255},
            },
        .pixelCount = 4,
    },
    {
        .id   = string_static("p3_color_per_line.ppm"),
        .text = string_static("P3\n"
                              "2\n2\n255\n"
                              "255\n0\n0\n"
                              "0\n255\n0\n"
                              "0\n0\n255\n"
                              "128\n128\n128\n"),
        .pixels =
            {
                {0, 0, 255, 255},
                {128, 128, 128, 255},
                {255, 0, 0, 255},
                {0, 255, 0, 255},
            },
        .pixelCount = 4,
    },
    {
        .id   = string_static("p3_single_line.ppm"),
        .text = string_static("P3 2 2 255 255 0 0 0 255 0 0 0 255 128 128 128"),
        .pixels =
            {
                {0, 0, 255, 255},
                {128, 128, 128, 255},
                {255, 0, 0, 255},
                {0, 255, 0, 255},
            },
        .pixelCount = 4,
    },
    {
        .id   = string_static("p3_commented.ppm"),
        .text = string_static("# Hello\nP3\n"
                              "# Comments\n2# Are\n2#Supported\n255#Everywhere\n"
                              "# In\n255# The\n0   # Format\n0 # Will\n"
                              "# That\n0 # Parse\n255 # Correctly?\n0\n"
                              "0 0 255\n"
                              "128 128 128# End of file\n"),
        .pixels =
            {
                {0, 0, 255, 255},
                {128, 128, 128, 255},
                {255, 0, 0, 255},
                {0, 255, 0, 255},
            },
        .pixelCount = 4,
    },
    {
        .id   = string_static("p3_windows_line-endings.ppm"),
        .text = string_static("P3\r\n"
                              "2 2 255\r\n"
                              "# Comments with windows line-endings\r\n"
                              "255 0 0\r\n"
                              "0 255 0\r\n"
                              "0 0 255\r\n"
                              "128 128 128\r\n"),
        .pixels =
            {
                {0, 0, 255, 255},
                {128, 128, 128, 255},
                {255, 0, 0, 255},
                {0, 255, 0, 255},
            },
        .pixelCount = 4,
    },
    {
        .id   = string_static("p3_unspecified_colors.ppm"),
        .text = string_static("P3 2 2 255\n"
                              "255 0 0\n"
                              "0 255 0"),
        .pixels =
            {
                {0, 0, 0, 255},
                {0, 0, 0, 255},
                {255, 0, 0, 255},
                {0, 255, 0, 255},
            },
        .pixelCount = 4,
    },
    {
        .id   = string_static("p3_extra_colors.ppm"),
        .text = string_static("P3 1 1 255\n"
                              "255 0 0\n"
                              "0 255 0\n"
                              "0 0 255"),
        .pixels =
            {
                {255, 0, 0, 255},
            },
        .pixelCount = 1,
    },
    {
        .id   = string_static("p6.ppm"),
        .text = string_static("P6 2 2 255\n"
                              "\xFF\x0\x0"
                              "\x0\xFF\x0"
                              "\x0\x0\xFF"
                              "\x80\x80\x80"),
        .pixels =
            {
                {0, 0, 255, 255},
                {128, 128, 128, 255},
                {255, 0, 0, 255},
                {0, 255, 0, 255},
            },
        .pixelCount = 4,
    },
    {
        .id   = string_static("p6_unspecified_colors.ppm"),
        .text = string_static("P6 2 2 255\n"
                              "\xFF\x1\x1"
                              "\x1\xFF\x1"
                              "\x1\x1\x1"
                              "\x1\x1\x1"),
        .pixels =
            {
                {1, 1, 1, 255},
                {1, 1, 1, 255},
                {255, 1, 1, 255},
                {1, 255, 1, 255},
            },
        .pixelCount = 4,
    },
    {
        .id   = string_static("p6_extra_colors.ppm"),
        .text = string_static("P6 1 1 255\n"
                              "\xFF\x1\x1"
                              "\x1\xFF\x1"
                              "\x1\x1\xFF"),
        .pixels =
            {
                {255, 1, 1, 255},
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
    def = ecs_def_create(g_alloc_heap);
    asset_register(def);
    ecs_register_module(def, loader_texture_ppm_test_module);

    world  = ecs_world_create(g_alloc_heap, def);
    runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_None);
  }

  it("can load ppm images") {
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
      const AssetTextureComp* tex = ecs_utils_read_t(world, AssetView, asset, AssetTextureComp);
      check_eq_int(tex->type, AssetTextureType_U8);
      check_eq_int(tex->channels, 4);
      check_require(tex->height * tex->height == g_testData[i].pixelCount);
      for (usize p = 0; p != g_testData[i].pixelCount; ++p) {
        check_eq_int(tex->pixelsB4[p].r, g_testData[i].pixels[p].r);
        check_eq_int(tex->pixelsB4[p].g, g_testData[i].pixels[p].g);
        check_eq_int(tex->pixelsB4[p].b, g_testData[i].pixels[p].b);
        check_eq_int(tex->pixelsB4[p].a, g_testData[i].pixels[p].a);
      }
    };
  }

  it("can unload ppm texture assets") {
    const AssetMemRecord record = {.id = string_lit("tex.ppm"), .data = g_testData[0].text};
    asset_manager_create_mem(world, AssetManagerFlags_None, &record, 1);
    ecs_world_flush(world);

    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
    const EcsEntityId asset   = asset_lookup(world, manager, string_lit("tex.ppm"));

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
