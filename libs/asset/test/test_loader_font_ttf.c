#include "asset.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_base64.h"
#include "ecs.h"

#include "utils_internal.h"

/**
 * Fonts exported from fontforge (sha: c3468cbd0320c152c0cbf762b9e2b63642d9c65f) and base64 encoded.
 */

static const struct {
  String id;
  String base64Data;
} g_testData[] = {
    {
        .id         = string_static("test.ttf"),
        .base64Data = string_static(
            "AAEAAAAOAIAAAwBgRkZUTZKGfgsAAAXMAAAAHEdERUYAFQAUAAAFsAAAABxPUy8yYqNs7QAAAWgAAABgY21hcA"
            "APA98AAAHYAAABQmN2dCAARAURAAADHAAAAARnYXNw//"
            "8AAwAABagAAAAIZ2x5Zo6zAJ8AAAMsAAAAdGhlYWQafppxAAAA7AAAADZoaGVhCiYIBQAAASQAAAAkaG10eBgA"
            "BCwAAAHIAAAAEGxvY2EAZgBYAAADIAAAAAptYXhwAEgAOQAAAUgAAAAgbmFtZZKIeQUAAAOgAAAB0XBvc3TMWO"
            "idAAAFdAAAADQAAQAAAAEAAAxB/"
            "+9fDzz1AAsIAAAAAADbgiVLAAAAANuCKtQARAAABBgFVQAAAAgAAgAAAAAAAAABAAAFVQAAALgIAAAAAAAEGAA"
            "BAAAAAAAAAAAAAAAAAAAABAABAAAABAAIAAIAAAAAAAIAAAABAAEAAABAAC4AAAAAAAQIAAGQAAUAAAUzBZkAA"
            "AEeBTMFmQAAA9cAZgISAAACAAUJAAAAAAAAAAAAAQAAAAAAAAAAAAAAAFBmRWQAwAAxADEGZv5mALgFVQAAAAA"
            "AAQAAAAAAAAAAAAAAIAABCAAARAAAAAAIAAAACAAD6AAAAAMAAAADAAAAHAABAAAAAAA8AAMAAQAAABwABAAgA"
            "AAABAAEAAEAAAAx//8AAAAx////"
            "0gABAAAAAAAAAQYAAAEAAAAAAAAAAQIAAAACAAAAAAAAAAAAAAAAAAAAAQAAAAAAAAAAAAAAAAAAAAAAAAADAA"
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
            "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
            "AAAAAAAAAAAAAAAAAABEBREAAAAsACwALAA6AAAAAgBEAAACZAVVAAMABwAusQEALzyyBwQA7TKxBgXcPLIDAg"
            "DtMgCxAwAvPLIFBADtMrIHBgH8PLIBAgDtMjMRIRElIREhRAIg/"
            "iQBmP5oBVX6q0QEzQAAAAED6AAABBgEAAADAAAhETMRA+"
            "gwBAD8AAAAAAAAAA4ArgABAAAAAAAAABsAOAABAAAAAAABAAQAXgABAAAAAAACAAcAcwABAAAAAAADABwAtQAB"
            "AAAAAAAEAAQA3AABAAAAAAAFABABAwABAAAAAAAGAAQBHgADAAEECQAAADYAAAADAAEECQABAAgAVAADAAEECQ"
            "ACAA4AYwADAAEECQADADgAewADAAEECQAEAAgA0gADAAEECQAFACAA4QADAAEECQAGAAgBFABDAG8AcAB5AHIA"
            "aQBnAGgAdAAgACgAYwApACAAMgAwADIAMAAsACAAYgBhAHMAdABpAGEAbgAAQ29weXJpZ2h0IChjKSAyMDIwLC"
            "BiYXN0aWFuAAB0AGUAcwB0AAB0ZXN0AABSAGUAZwB1AGwAYQByAABSZWd1bGFyAABGAG8AbgB0AEYAbwByAGcA"
            "ZQAgADoAIAB0AGUAcwB0ACAAOgAgADEAMgAtADkALQAyADAAMgAwAABGb250Rm9yZ2UgOiB0ZXN0IDogMTItOS"
            "0yMDIwAAB0AGUAcwB0AAB0ZXN0AABWAGUAcgBzAGkAbwBuACAAMAAwADEALgAwADAAMAAgAABWZXJzaW9uIDAw"
            "MS4wMDAgAAB0AGUAcwB0AAB0ZXN0AAAAAAACAAAAAAAA/"
            "2cAZgAAAAEAAAAAAAAAAAAAAAAAAAAAAAQAAAABAAIBAglnbHlwaF9vbmUAAAAB//"
            "8AAgABAAAAAAAAAAwAFAAEAAAAAgAAAAEAAAABAAAAAAABAAAAANuCLesAAAAA24IlSwAAAADbgirU"),
    },
};

static const struct {
  String id;
  String text;
} g_errorTestData[] = {
    {
        .id   = string_static("invalid.ttf"),
        .text = string_static("Hello World"),
    },
};

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(AssetView) { ecs_access_read(AssetFontComp); }

ecs_module_init(loader_font_ttf_test_module) {
  ecs_register_view(ManagerView);
  ecs_register_view(AssetView);
}

spec(loader_font_ttf) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    asset_register(def);
    ecs_register_module(def, loader_font_ttf_test_module);

    world  = ecs_world_create(g_alloc_heap, def);
    runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_None);
  }

  skip_it("can load TrueType fonts") {
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
      const AssetFontComp* font = ecs_utils_read_t(world, AssetView, asset, AssetFontComp);
      (void)font;
    }

    array_for_t(records, AssetMemRecord, rec) { string_free(g_alloc_heap, rec->data); }
  }

  skip_it("can unload TrueType font assets") {
    const AssetMemRecord record = {
        .id   = string_lit("font.ttf"),
        .data = string_dup(g_alloc_heap, base64_decode_scratch(g_testData[0].base64Data)),
    };
    asset_manager_create_mem(world, AssetManagerFlags_None, &record, 1);
    ecs_world_flush(world);

    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
    const EcsEntityId asset   = asset_lookup(world, manager, string_lit("font.ttf"));

    asset_acquire(world, asset);
    asset_test_wait(runner);
    check(ecs_world_has_t(world, asset, AssetFontComp));

    asset_release(world, asset);
    asset_test_wait(runner);
    check(!ecs_world_has_t(world, asset, AssetFontComp));

    string_free(g_alloc_heap, record.data);
  }

  it("fails when loading invalid TrueType font files") {
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
      check(!ecs_world_has_t(world, asset, AssetFontComp));
    }
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
