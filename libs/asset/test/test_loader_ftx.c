#include "asset.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_base64.h"
#include "ecs.h"

#include "utils_internal.h"

/**
 * Font exported from fontforge (sha: c3468cbd0320c152c0cbf762b9e2b63642d9c65f) and base64 encoded.
 */
static const String g_testFontBase64 = string_static(
    "AAEAAAAOAIAAAwBgRkZUTZKGfgsAAAXMAAAAHEdERUYAFQAUAAAFsAAAABxPUy8yYqNs7QAAAWgAAABgY21hcAAPA98AAA"
    "HYAAABQmN2dCAARAURAAADHAAAAARnYXNw//8AAwAABagAAAAIZ2x5Zo6zAJ8AAAMsAAAAdGhlYWQafppxAAAA7AAAADZo"
    "aGVhCiYIBQAAASQAAAAkaG10eBgABCwAAAHIAAAAEGxvY2EAZgBYAAADIAAAAAptYXhwAEgAOQAAAUgAAAAgbmFtZZKIeQ"
    "UAAAOgAAAB0XBvc3TMWOidAAAFdAAAADQAAQAAAAEAAAxB/+9fDzz1AAsIAAAAAADbgiVLAAAAANuCKtQARAAABBgFVQAA"
    "AAgAAgAAAAAAAAABAAAFVQAAALgIAAAAAAAEGAABAAAAAAAAAAAAAAAAAAAABAABAAAABAAIAAIAAAAAAAIAAAABAAEAAA"
    "BAAC4AAAAAAAQIAAGQAAUAAAUzBZkAAAEeBTMFmQAAA9cAZgISAAACAAUJAAAAAAAAAAAAAQAAAAAAAAAAAAAAAFBmRWQA"
    "wAAxADEGZv5mALgFVQAAAAAAAQAAAAAAAAAAAAAAIAABCAAARAAAAAAIAAAACAAD6AAAAAMAAAADAAAAHAABAAAAAAA8AA"
    "MAAQAAABwABAAgAAAABAAEAAEAAAAx//8AAAAx////0gABAAAAAAAAAQYAAAEAAAAAAAAAAQIAAAACAAAAAAAAAAAAAAAA"
    "AAAAAQAAAAAAAAAAAAAAAAAAAAAAAAADAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "AAAAAAAAAAAAAAAAAAAAAAAAAAAABEBREAAAAsACwALAA6AAAAAgBEAAACZAVVAAMABwAusQEALzyyBwQA7TKxBgXcPLID"
    "AgDtMgCxAwAvPLIFBADtMrIHBgH8PLIBAgDtMjMRIRElIREhRAIg/iQBmP5oBVX6q0QEzQAAAAED6AAABBgEAAADAAAhET"
    "MRA+gwBAD8AAAAAAAAAA4ArgABAAAAAAAAABsAOAABAAAAAAABAAQAXgABAAAAAAACAAcAcwABAAAAAAADABwAtQABAAAA"
    "AAAEAAQA3AABAAAAAAAFABABAwABAAAAAAAGAAQBHgADAAEECQAAADYAAAADAAEECQABAAgAVAADAAEECQACAA4AYwADAA"
    "EECQADADgAewADAAEECQAEAAgA0gADAAEECQAFACAA4QADAAEECQAGAAgBFABDAG8AcAB5AHIAaQBnAGgAdAAgACgAYwAp"
    "ACAAMgAwADIAMAAsACAAYgBhAHMAdABpAGEAbgAAQ29weXJpZ2h0IChjKSAyMDIwLCBiYXN0aWFuAAB0AGUAcwB0AAB0ZX"
    "N0AABSAGUAZwB1AGwAYQByAABSZWd1bGFyAABGAG8AbgB0AEYAbwByAGcAZQAgADoAIAB0AGUAcwB0ACAAOgAgADEAMgAt"
    "ADkALQAyADAAMgAwAABGb250Rm9yZ2UgOiB0ZXN0IDogMTItOS0yMDIwAAB0AGUAcwB0AAB0ZXN0AABWAGUAcgBzAGkAbw"
    "BuACAAMAAwADEALgAwADAAMAAgAABWZXJzaW9uIDAwMS4wMDAgAAB0AGUAcwB0AAB0ZXN0AAAAAAACAAAAAAAA/2cAZgAA"
    "AAEAAAAAAAAAAAAAAAAAAAAAAAQAAAABAAIBAglnbHlwaF9vbmUAAAAB//8AAgABAAAAAAAAAAwAFAAEAAAAAgAAAAEAAA"
    "ABAAAAAAABAAAAANuCLesAAAAA24IlSwAAAADbgirU");

static const AssetMemRecord g_testData[] = {
    {
        .id   = string_static("test.ftx"),
        .data = string_static("{"
                              "  \"size\": 64,"
                              "  \"glyphSize\": 32,"
                              "  \"border\": 3,"
                              "  \"baseline\": 0.3,"
                              "  \"fonts\": [{ \"id\": \"font.ttf\", \"characters\": \"1\"}]"
                              "}"),
    },
};

static const AssetMemRecord g_errorTestData[] = {
    {
        .id   = string_static("no-font.ftx"),
        .data = string_static("{"
                              "  \"size\": 64,"
                              "  \"glyphSize\": 32,"
                              "  \"border\": 3,"
                              "  \"baseline\": 0.3,"
                              "  \"fonts\": [{ \"characters\": \"1\"}]"
                              "}"),
    },
    {
        .id   = string_static("empty-font.ftx"),
        .data = string_static("{"
                              "  \"size\": 64,"
                              "  \"glyphSize\": 32,"
                              "  \"border\": 3,"
                              "  \"baseline\": 0.3,"
                              "  \"fonts\": [{ \"id\": \"\", \"characters\": \"1\"}]"
                              "}"),
    },
    {
        .id   = string_static("missing-font.ftx"),
        .data = string_static("{"
                              "  \"size\": 64,"
                              "  \"glyphSize\": 32,"
                              "  \"border\": 3,"
                              "  \"baseline\": 0.3,"
                              "  \"fonts\": [{ \"id\": \"missing.ttf\", \"characters\": \"1\"}]"
                              "}"),
    },
    {
        .id   = string_static("non-pow2-size.ftx"),
        .data = string_static("{"
                              "  \"size\": 42,"
                              "  \"glyphSize\": 32,"
                              "  \"border\": 3,"
                              "  \"baseline\": 0.3,"
                              "  \"fonts\": [{ \"id\": \"font.ttf\", \"characters\": \"1\"}]"
                              "}"),
    },
    {
        .id   = string_static("too-many-glyphs.ftx"),
        .data = string_static("{"
                              "  \"size\": 64,"
                              "  \"glyphSize\": 32,"
                              "  \"border\": 3,"
                              "  \"baseline\": 0.3,"
                              "  \"fonts\": [{ \"id\": \"font.ttf\", \"characters\": \"1111\"}]"
                              "}"),
    },
};

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(AssetView) {
  ecs_access_read(AssetFtxComp);
  ecs_access_read(AssetTextureComp);
}

ecs_module_init(loader_ftx_test_module) {
  ecs_register_view(ManagerView);
  ecs_register_view(AssetView);
}

spec(loader_ftx) {
  EcsDef*    def          = null;
  EcsWorld*  world        = null;
  EcsRunner* runner       = null;
  String     testFontData = string_empty;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    asset_register(def);
    ecs_register_module(def, loader_ftx_test_module);

    world  = ecs_world_create(g_alloc_heap, def);
    runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_None);

    testFontData = string_dup(g_alloc_heap, base64_decode_scratch(g_testFontBase64));
  }

  it("can load ftx assets") {
    AssetMemRecord records[array_elems(g_testData) + 1] = {
        {.id = string_lit("font.ttf"), .data = testFontData},
    };
    for (usize i = 0; i != array_elems(g_testData); ++i) {
      records[i + 1] = g_testData[i];
    }
    asset_manager_create_mem(world, AssetManagerFlags_None, records, array_elems(records));
    ecs_world_flush(world);

    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);

    const EcsEntityId asset = asset_lookup(world, manager, string_lit("test.ftx"));
    asset_acquire(world, asset);

    asset_test_wait(runner);

    check_require(ecs_world_has_t(world, asset, AssetLoadedComp));
    const AssetFtxComp*     ftx = ecs_utils_read_t(world, AssetView, asset, AssetFtxComp);
    const AssetTextureComp* tex = ecs_utils_read_t(world, AssetView, asset, AssetTextureComp);

    check_require(ftx->characterCount == 2);
    check_eq_int(ftx->characters[0].cp, 0); // The 'missing' character.
    check_eq_int(ftx->characters[0].glyphIndex, 0);

    check_eq_int(ftx->characters[1].cp, 0x31); // The 'digit one' character.
    check_eq_int(ftx->characters[1].glyphIndex, 1);

    check_eq_int(tex->type, AssetTextureType_Byte);
    check_eq_int(tex->channels, 1);
    check_eq_int(tex->width, 64);
    check_eq_int(tex->height, 64);
  }

  it("can unload ftx assets") {
    const AssetMemRecord records[] = {
        {.id = string_lit("font.ttf"), .data = testFontData},
        {.id = string_lit("test.ftx"), .data = g_testData[0].data},
    };
    asset_manager_create_mem(world, AssetManagerFlags_None, records, array_elems(records));
    ecs_world_flush(world);

    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
    const EcsEntityId asset   = asset_lookup(world, manager, string_lit("test.ftx"));

    asset_acquire(world, asset);
    asset_test_wait(runner);

    check(ecs_world_has_t(world, asset, AssetFtxComp));
    check(ecs_world_has_t(world, asset, AssetTextureComp));

    asset_release(world, asset);
    asset_test_wait(runner);

    check(!ecs_world_has_t(world, asset, AssetFtxComp));
    check(!ecs_world_has_t(world, asset, AssetTextureComp));
  }

  it("fails when loading invalid ftx files") {
    AssetMemRecord records[array_elems(g_errorTestData) + 1] = {
        {.id = string_lit("font.ttf"), .data = testFontData},
    };
    for (usize i = 0; i != array_elems(g_errorTestData); ++i) {
      records[i + 1] = g_errorTestData[i];
    }
    asset_manager_create_mem(world, AssetManagerFlags_None, records, array_elems(records));
    ecs_world_flush(world);

    array_for_t(g_errorTestData, AssetMemRecord, errRec) {
      AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
      const EcsEntityId asset   = asset_lookup(world, manager, errRec->id);
      asset_acquire(world, asset);
      asset_test_wait(runner);

      check(ecs_world_has_t(world, asset, AssetFailedComp));
      check(!ecs_world_has_t(world, asset, AssetFtxComp));
      check(!ecs_world_has_t(world, asset, AssetTextureComp));
    }
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
    alloc_free(g_alloc_heap, testFontData);
  }
}
