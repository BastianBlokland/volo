#include "asset.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_base64.h"
#include "ecs.h"

#include "utils_internal.h"

static const struct {
  String id, data;
  bool   base64Encoded;
} g_testData[] = {
    {
        .id   = string_static("test.ttf"),
        .data = string_static(
            "AAEAAAAOAIAAAwBgRkZUTZKGfgsAAAXMAAAAHEdERUYAFQAUAAAFsAAAABxPUy8yYqNs7QAAAWgAAABgY21hcA"
            "APA98AAAHYAAABQmN2dCAARAURAAADHAAAAARnYXNw//8AAwAABagAAAAIZ2x5Zo6zAJ8AAAMsAAAAdGhlYWQa"
            "fppxAAAA7AAAADZoaGVhCiYIBQAAASQAAAAkaG10eBgABCwAAAHIAAAAEGxvY2EAZgBYAAADIAAAAAptYXhwAE"
            "gAOQAAAUgAAAAgbmFtZZKIeQUAAAOgAAAB0XBvc3TMWOidAAAFdAAAADQAAQAAAAEAAAxB/+9fDzz1AAsIAAAA"
            "AADbgiVLAAAAANuCKtQARAAABBgFVQAAAAgAAgAAAAAAAAABAAAFVQAAALgIAAAAAAAEGAABAAAAAAAAAAAAAA"
            "AAAAAABAABAAAABAAIAAIAAAAAAAIAAAABAAEAAABAAC4AAAAAAAQIAAGQAAUAAAUzBZkAAAEeBTMFmQAAA9cA"
            "ZgISAAACAAUJAAAAAAAAAAAAAQAAAAAAAAAAAAAAAFBmRWQAwAAxADEGZv5mALgFVQAAAAAAAQAAAAAAAAAAAA"
            "AAIAABCAAARAAAAAAIAAAACAAD6AAAAAMAAAADAAAAHAABAAAAAAA8AAMAAQAAABwABAAgAAAABAAEAAEAAAAx"
            "//8AAAAx////0gABAAAAAAAAAQYAAAEAAAAAAAAAAQIAAAACAAAAAAAAAAAAAAAAAAAAAQAAAAAAAAAAAAAAAA"
            "AAAAAAAAADAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAABEBREAAAAsACwALAA6AAAAAgBEAAACZAVVAAMABwAusQEALzyyBwQA7T"
            "KxBgXcPLIDAgDtMgCxAwAvPLIFBADtMrIHBgH8PLIBAgDtMjMRIRElIREhRAIg/iQBmP5oBVX6q0QEzQAAAAED"
            "6AAABBgEAAADAAAhETMRA+gwBAD8AAAAAAAAAA4ArgABAAAAAAAAABsAOAABAAAAAAABAAQAXgABAAAAAAACAA"
            "cAcwABAAAAAAADABwAtQABAAAAAAAEAAQA3AABAAAAAAAFABABAwABAAAAAAAGAAQBHgADAAEECQAAADYAAAAD"
            "AAEECQABAAgAVAADAAEECQACAA4AYwADAAEECQADADgAewADAAEECQAEAAgA0gADAAEECQAFACAA4QADAAEECQ"
            "AGAAgBFABDAG8AcAB5AHIAaQBnAGgAdAAgACgAYwApACAAMgAwADIAMAAsACAAYgBhAHMAdABpAGEAbgAAQ29w"
            "eXJpZ2h0IChjKSAyMDIwLCBiYXN0aWFuAAB0AGUAcwB0AAB0ZXN0AABSAGUAZwB1AGwAYQByAABSZWd1bGFyAA"
            "BGAG8AbgB0AEYAbwByAGcAZQAgADoAIAB0AGUAcwB0ACAAOgAgADEAMgAtADkALQAyADAAMgAwAABGb250Rm9y"
            "Z2UgOiB0ZXN0IDogMTItOS0yMDIwAAB0AGUAcwB0AAB0ZXN0AABWAGUAcgBzAGkAbwBuACAAMAAwADEALgAwAD"
            "AAMAAgAABWZXJzaW9uIDAwMS4wMDAgAAB0AGUAcwB0AAB0ZXN0AAAAAAACAAAAAAAA/2cAZgAAAAEAAAAAAAAA"
            "AAAAAAAAAAAAAAQAAAABAAIBAglnbHlwaF9vbmUAAAAB//8AAgABAAAAAAAAAAwAFAAEAAAAAgAAAAEAAAABAA"
            "AAAAABAAAAANuCLesAAAAA24IlSwAAAADbgirU"),
        .base64Encoded = true,
    },
    {
        .id   = string_static("test.ftx"),
        .data = string_static("{"
                              "  \"fontId\": \"test.ttf\","
                              "  \"size\": 64,"
                              "  \"glyphSize\": 32,"
                              "  \"border\": 3,"
                              "  \"characters\": \"1\","
                              "  \"depth\": \"Less\","
                              "  \"cull\": \"Back\","
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
  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    asset_register(def);
    ecs_register_module(def, loader_ftx_test_module);

    world  = ecs_world_create(g_alloc_heap, def);
    runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_None);
  }

  it("can load ftx assets") {
    AssetMemRecord records[array_elems(g_testData)];
    for (usize i = 0; i != array_elems(g_testData); ++i) {
      records[i] = (AssetMemRecord){
          .id   = g_testData[i].id,
          .data = g_testData[i].base64Encoded
                      ? string_dup(g_alloc_heap, base64_decode_scratch(g_testData[i].data))
                      : g_testData[i].data,
      };
    }
    asset_manager_create_mem(world, AssetManagerFlags_None, records, array_elems(g_testData));
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

    check_eq_int(tex->width, 64);
    check_eq_int(tex->height, 64);

    for (usize i = 0; i != array_elems(g_testData); ++i) {
      if (g_testData[i].base64Encoded) {
        alloc_free(g_alloc_heap, records[i].data);
      }
    }
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
