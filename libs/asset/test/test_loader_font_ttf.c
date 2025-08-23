#include "asset/font.h"
#include "asset/manager.h"
#include "asset/register.h"
#include "check/spec.h"
#include "core/alloc.h"
#include "core/base64.h"
#include "ecs/utils.h"
#include "ecs/world.h"

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
    },
};

static const struct {
  String id;
  String text;
} g_errorTestData[] = {
    {
        .id   = string_static("invalid.ttf"),
        .text = string_static("Hello Beautiful World"),
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
    def = ecs_def_create(g_allocHeap);
    asset_register(def);
    ecs_register_module(def, loader_font_ttf_test_module);

    world  = ecs_world_create(g_allocHeap, def);
    runner = ecs_runner_create(g_allocHeap, world, EcsRunnerFlags_None);
  }

  it("can load TrueType fonts") {
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
      const AssetFontComp*  font  = ecs_utils_read_t(world, AssetView, asset, AssetFontComp);
      const AssetFontGlyph* glyph = asset_font_lookup(font, 0x31); // 'digit one'.

      /**
       * Glyph is a box consisting of 4 points and 4 lines connecting the edges of the box.
       */

      check_require(glyph->segmentCount == 4);

      check(font->segments.values[glyph->segmentIndex + 0].type == AssetFontSegment_Line);
      const u32 seg1P1 = font->segments.values[glyph->segmentIndex + 0].pointIndex + 0;
      const u32 seg1P2 = font->segments.values[glyph->segmentIndex + 0].pointIndex + 1;

      check(font->segments.values[glyph->segmentIndex + 1].type == AssetFontSegment_Line);
      const u32 seg2P1 = font->segments.values[glyph->segmentIndex + 1].pointIndex + 0;
      const u32 seg2P2 = font->segments.values[glyph->segmentIndex + 1].pointIndex + 1;

      check(font->segments.values[glyph->segmentIndex + 2].type == AssetFontSegment_Line);
      const u32 seg3P1 = font->segments.values[glyph->segmentIndex + 2].pointIndex + 0;
      const u32 seg3P2 = font->segments.values[glyph->segmentIndex + 2].pointIndex + 1;

      check(font->segments.values[glyph->segmentIndex + 3].type == AssetFontSegment_Line);
      const u32 seg4P1 = font->segments.values[glyph->segmentIndex + 3].pointIndex + 0;
      const u32 seg4P2 = font->segments.values[glyph->segmentIndex + 3].pointIndex + 1;

      check_eq_int(seg1P1, seg1P2 - 1);
      check_eq_int(seg1P2, seg2P1);
      check_eq_int(seg2P2, seg3P1);
      check_eq_int(seg3P2, seg4P1);
      check_eq_int(seg4P2, seg4P1 + 1);

      check_eq_float(font->points.values[seg1P1].x, 0.4765625f, 1e-6);
      check_eq_float(font->points.values[seg1P1].y, 0, 1e-6);

      check_eq_float(font->points.values[seg2P1].x, 0.4765625f, 1e-6);
      check_eq_float(font->points.values[seg2P1].y, 1, 1e-6);

      check_eq_float(font->points.values[seg3P1].x, 0.5234375f, 1e-6);
      check_eq_float(font->points.values[seg3P1].y, 1, 1e-6);

      check_eq_float(font->points.values[seg4P1].x, 0.5234375f, 1e-6);
      check_eq_float(font->points.values[seg4P1].y, 0, 1e-6);
    }

    array_for_t(records, AssetMemRecord, rec) { string_free(g_allocHeap, rec->data); }
  }

  it("can unload TrueType font assets") {
    const AssetMemRecord record = {
        .id   = string_lit("font.ttf"),
        .data = string_dup(g_allocHeap, base64_decode_scratch(g_testData[0].base64Data)),
    };
    asset_manager_create_mem(world, AssetManagerFlags_None, &record, 1);
    ecs_world_flush(world);

    EcsEntityId asset;
    {
      AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
      asset                     = asset_lookup(world, manager, string_lit("font.ttf"));
    }
    asset_acquire(world, asset);

    asset_test_wait(runner);
    check(ecs_world_has_t(world, asset, AssetFontComp));

    asset_release(world, asset);
    asset_test_wait(runner);
    check(!ecs_world_has_t(world, asset, AssetFontComp));

    string_free(g_allocHeap, record.data);
  }

  it("fails when loading invalid TrueType font files") {
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
      check(!ecs_world_has_t(world, asset, AssetFontComp));
    }
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
