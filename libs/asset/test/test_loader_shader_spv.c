#include "asset.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_base64.h"
#include "ecs.h"

#include "utils_internal.h"

static const struct {
  String          id;
  String          base64Data;
  AssetShaderKind kind;
  String          entryPoint;
  AssetShaderRes  resources[16];
  usize           resourceCount;
  AssetShaderSpec specs[16];
  usize           specCount;
} g_testData[] = {
    {
        .id = string_static("vertex_v1-3.spv"),
        .base64Data =
            string_static("AwIjBwADAQAIAA0ABgAAAAAAAAARAAIAAQAAAAsABgABAAAAR0xTTC5zdGQuNDUwAAAAAA4A"
                          "AwAAAAAAAQAAAA8ABQAAAAAABAAAAG1haW4AAAAAEwACAAIAAAAhAAMAAwAAAAIAAAA2AAUA"
                          "AgAAAAQAAAAAAAAAAwAAAPgAAgAFAAAA/QABADgAAQA="),
        .kind       = AssetShaderKind_SpvVertex,
        .entryPoint = string_static("main"),
    },
    {
        .id         = string_static("fragment_v1-3.spv"),
        .base64Data = string_static(
            "AwIjBwADAQAIAA0ADAAAAAAAAAARAAIAAQAAAAsABgABAAAAR0xTTC5zdGQuNDUwAAAAAA4AAwAAAAAAAQAAAA"
            "8ABgAEAAAABAAAAG1haW4AAAAACQAAABAAAwAEAAAABwAAAAMAAwACAAAAwgEAAAQACQBHTF9BUkJfc2VwYXJh"
            "dGVfc2hhZGVyX29iamVjdHMAAAQACgBHTF9HT09HTEVfY3BwX3N0eWxlX2xpbmVfZGlyZWN0aXZlAAAEAAgAR0"
            "xfR09PR0xFX2luY2x1ZGVfZGlyZWN0aXZlAAUABAAEAAAAbWFpbgAAAAAFAAUACQAAAG91dENvbG9yAAAAAEcA"
            "BAAJAAAAHgAAAAAAAAATAAIAAgAAACEAAwADAAAAAgAAABYAAwAGAAAAIAAAABcABAAHAAAABgAAAAQAAAAgAA"
            "QACAAAAAMAAAAHAAAAOwAEAAgAAAAJAAAAAwAAACsABAAGAAAACgAAAAAAgD8sAAcABwAAAAsAAAAKAAAACgAA"
            "AAoAAAAKAAAANgAFAAIAAAAEAAAAAAAAAAMAAAD4AAIABQAAAD4AAwAJAAAACwAAAP0AAQA4AAEA"),
        .kind       = AssetShaderKind_SpvFragment,
        .entryPoint = string_static("main"),
    },
    {
        .id         = string_static("6-texture-inputs_vertex_v1-3.spv"),
        .base64Data = string_static(
            "AwIjBwADAQAIAA0AEAAAAAAAAAARAAIAAQAAAAsABgABAAAAR0xTTC5zdGQuNDUwAAAAAA4AAwAAAAAAAQAAAA"
            "8ABQAAAAAABAAAAG1haW4AAAAAAwADAAIAAADCAQAABAAJAEdMX0FSQl9zZXBhcmF0ZV9zaGFkZXJfb2JqZWN0"
            "cwAABAAKAEdMX0dPT0dMRV9jcHBfc3R5bGVfbGluZV9kaXJlY3RpdmUAAAQACABHTF9HT09HTEVfaW5jbHVkZV"
            "9kaXJlY3RpdmUABQAEAAQAAABtYWluAAAAAAUABAAKAAAAdGV4MQAAAAAFAAQACwAAAHRleDIAAAAABQAEAAwA"
            "AAB0ZXgzAAAAAAUABAANAAAAdGV4NAAAAAAFAAQADgAAAHRleDUAAAAABQAEAA8AAAB0ZXg2AAAAAEcABAAKAA"
            "AAIgAAAAAAAABHAAQACgAAACEAAAAAAAAARwAEAAsAAAAiAAAAAgAAAEcABAALAAAAIQAAAAAAAABHAAQADAAA"
            "ACIAAAACAAAARwAEAAwAAAAhAAAAAQAAAEcABAANAAAAIgAAAAQAAABHAAQADQAAACEAAAAAAAAARwAEAA4AAA"
            "AiAAAABAAAAEcABAAOAAAAIQAAAAEAAABHAAQADwAAACIAAAAEAAAARwAEAA8AAAAhAAAABwAAABMAAgACAAAA"
            "IQADAAMAAAACAAAAFgADAAYAAAAgAAAAGQAJAAcAAAAGAAAAAQAAAAAAAAAAAAAAAAAAAAEAAAAAAAAAGwADAA"
            "gAAAAHAAAAIAAEAAkAAAAAAAAACAAAADsABAAJAAAACgAAAAAAAAA7AAQACQAAAAsAAAAAAAAAOwAEAAkAAAAM"
            "AAAAAAAAADsABAAJAAAADQAAAAAAAAA7AAQACQAAAA4AAAAAAAAAOwAEAAkAAAAPAAAAAAAAADYABQACAAAABA"
            "AAAAAAAAADAAAA+AACAAUAAAD9AAEAOAABAA=="),
        .kind       = AssetShaderKind_SpvVertex,
        .entryPoint = string_static("main"),
        .resources =
            {
                {AssetShaderResKind_Texture, .set = 0, .binding = 0},
                {AssetShaderResKind_Texture, .set = 2, .binding = 0},
                {AssetShaderResKind_Texture, .set = 2, .binding = 1},
                {AssetShaderResKind_Texture, .set = 4, .binding = 0},
                {AssetShaderResKind_Texture, .set = 4, .binding = 1},
                {AssetShaderResKind_Texture, .set = 4, .binding = 7},
            },
        .resourceCount = 6,
    },
    {
        .id         = string_static("6-uniformbuffer-inputs_vertex_v1-3.spv"),
        .base64Data = string_static(
            "AwIjBwADAQAIAA0AIwAAAAAAAAARAAIAAQAAAAsABgABAAAAR0xTTC5zdGQuNDUwAAAAAA4AAwAAAAAAAQAAAA"
            "8ABQAAAAAABAAAAG1haW4AAAAAAwADAAIAAADCAQAABAAJAEdMX0FSQl9zZXBhcmF0ZV9zaGFkZXJfb2JqZWN0"
            "cwAABAAKAEdMX0dPT0dMRV9jcHBfc3R5bGVfbGluZV9kaXJlY3RpdmUAAAQACABHTF9HT09HTEVfaW5jbHVkZV"
            "9kaXJlY3RpdmUABQAEAAQAAABtYWluAAAAAAUABAAIAAAARGF0YQAAAAAGAAcACAAAAAAAAABtZWFuaW5nT2ZM"
            "aWZlAAAABQAFAAwAAABEYXRhQnVmZmVyMQAGAAUADAAAAAAAAABkYXRhAAAAAAUAAwAOAAAAZDEAAAUABQAQAA"
            "AARGF0YUJ1ZmZlcjIABgAFABAAAAAAAAAAZGF0YQAAAAAFAAMAEgAAAGQyAAAFAAUAFAAAAERhdGFCdWZmZXIz"
            "AAYABQAUAAAAAAAAAGRhdGEAAAAABQADABYAAABkMwAABQAFABgAAABEYXRhQnVmZmVyNAAGAAUAGAAAAAAAAA"
            "BkYXRhAAAAAAUAAwAaAAAAZDQAAAUABQAcAAAARGF0YUJ1ZmZlcjUABgAFABwAAAAAAAAAZGF0YQAAAAAFAAMA"
            "HgAAAGQ1AAAFAAUAIAAAAERhdGFCdWZmZXI2AAYABQAgAAAAAAAAAGRhdGEAAAAABQADACIAAABkNgAASAAFAA"
            "gAAAAAAAAAIwAAAAAAAABHAAQACwAAAAYAAAAQAAAASAAFAAwAAAAAAAAAIwAAAAAAAABHAAMADAAAAAIAAABH"
            "AAQADgAAACIAAAAAAAAARwAEAA4AAAAhAAAAAAAAAEcABAAPAAAABgAAABAAAABIAAUAEAAAAAAAAAAjAAAAAA"
            "AAAEcAAwAQAAAAAgAAAEcABAASAAAAIgAAAAIAAABHAAQAEgAAACEAAAAAAAAARwAEABMAAAAGAAAAEAAAAEgA"
            "BQAUAAAAAAAAACMAAAAAAAAARwADABQAAAACAAAARwAEABYAAAAiAAAAAgAAAEcABAAWAAAAIQAAAAEAAABHAA"
            "QAFwAAAAYAAAAQAAAASAAFABgAAAAAAAAAIwAAAAAAAABHAAMAGAAAAAIAAABHAAQAGgAAACIAAAAEAAAARwAE"
            "ABoAAAAhAAAAAAAAAEcABAAbAAAABgAAABAAAABIAAUAHAAAAAAAAAAjAAAAAAAAAEcAAwAcAAAAAgAAAEcABA"
            "AeAAAAIgAAAAQAAABHAAQAHgAAACEAAAABAAAARwAEAB8AAAAGAAAAEAAAAEgABQAgAAAAAAAAACMAAAAAAAAA"
            "RwADACAAAAACAAAARwAEACIAAAAiAAAABAAAAEcABAAiAAAAIQAAAAcAAAATAAIAAgAAACEAAwADAAAAAgAAAB"
            "YAAwAGAAAAIAAAABcABAAHAAAABgAAAAQAAAAeAAMACAAAAAcAAAAVAAQACQAAACAAAAAAAAAAKwAEAAkAAAAK"
            "AAAAAQAAABwABAALAAAACAAAAAoAAAAeAAMADAAAAAsAAAAgAAQADQAAAAIAAAAMAAAAOwAEAA0AAAAOAAAAAg"
            "AAABwABAAPAAAACAAAAAoAAAAeAAMAEAAAAA8AAAAgAAQAEQAAAAIAAAAQAAAAOwAEABEAAAASAAAAAgAAABwA"
            "BAATAAAACAAAAAoAAAAeAAMAFAAAABMAAAAgAAQAFQAAAAIAAAAUAAAAOwAEABUAAAAWAAAAAgAAABwABAAXAA"
            "AACAAAAAoAAAAeAAMAGAAAABcAAAAgAAQAGQAAAAIAAAAYAAAAOwAEABkAAAAaAAAAAgAAABwABAAbAAAACAAA"
            "AAoAAAAeAAMAHAAAABsAAAAgAAQAHQAAAAIAAAAcAAAAOwAEAB0AAAAeAAAAAgAAABwABAAfAAAACAAAAAoAAA"
            "AeAAMAIAAAAB8AAAAgAAQAIQAAAAIAAAAgAAAAOwAEACEAAAAiAAAAAgAAADYABQACAAAABAAAAAAAAAADAAAA"
            "+AACAAUAAAD9AAEAOAABAA=="),
        .kind       = AssetShaderKind_SpvVertex,
        .entryPoint = string_static("main"),
        .resources =
            {
                {AssetShaderResKind_UniformBuffer, .set = 0, .binding = 0},
                {AssetShaderResKind_UniformBuffer, .set = 2, .binding = 0},
                {AssetShaderResKind_UniformBuffer, .set = 2, .binding = 1},
                {AssetShaderResKind_UniformBuffer, .set = 4, .binding = 0},
                {AssetShaderResKind_UniformBuffer, .set = 4, .binding = 1},
                {AssetShaderResKind_UniformBuffer, .set = 4, .binding = 7},
            },
        .resourceCount = 6,
    },
    {
        .id         = string_static("6-storagebuffer-inputs_vertex_v1-3.spv"),
        .base64Data = string_static(
            "AwIjBwADAQAIAA0AIQAAAAAAAAARAAIAAQAAAAsABgABAAAAR0xTTC5zdGQuNDUwAAAAAA4AAwAAAAAAAQAAAA"
            "8ABQAAAAAABAAAAG1haW4AAAAAAwADAAIAAADCAQAABAAJAEdMX0FSQl9zZXBhcmF0ZV9zaGFkZXJfb2JqZWN0"
            "cwAABAAKAEdMX0dPT0dMRV9jcHBfc3R5bGVfbGluZV9kaXJlY3RpdmUAAAQACABHTF9HT09HTEVfaW5jbHVkZV"
            "9kaXJlY3RpdmUABQAEAAQAAABtYWluAAAAAAUABAAIAAAARGF0YQAAAAAGAAcACAAAAAAAAABtZWFuaW5nT2ZM"
            "aWZlAAAABQAFAAoAAABEYXRhQnVmZmVyMQAGAAUACgAAAAAAAABkYXRhAAAAAAUAAwAMAAAAZDEAAAUABQAOAA"
            "AARGF0YUJ1ZmZlcjIABgAFAA4AAAAAAAAAZGF0YQAAAAAFAAMAEAAAAGQyAAAFAAUAEgAAAERhdGFCdWZmZXIz"
            "AAYABQASAAAAAAAAAGRhdGEAAAAABQADABQAAABkMwAABQAFABYAAABEYXRhQnVmZmVyNAAGAAUAFgAAAAAAAA"
            "BkYXRhAAAAAAUAAwAYAAAAZDQAAAUABQAaAAAARGF0YUJ1ZmZlcjUABgAFABoAAAAAAAAAZGF0YQAAAAAFAAMA"
            "HAAAAGQ1AAAFAAUAHgAAAERhdGFCdWZmZXI2AAYABQAeAAAAAAAAAGRhdGEAAAAABQADACAAAABkNgAASAAFAA"
            "gAAAAAAAAAIwAAAAAAAABHAAQACQAAAAYAAAAQAAAASAAEAAoAAAAAAAAAGAAAAEgABQAKAAAAAAAAACMAAAAA"
            "AAAARwADAAoAAAACAAAARwAEAAwAAAAiAAAAAAAAAEcABAAMAAAAIQAAAAAAAABHAAQADQAAAAYAAAAQAAAASA"
            "AEAA4AAAAAAAAAGAAAAEgABQAOAAAAAAAAACMAAAAAAAAARwADAA4AAAACAAAARwAEABAAAAAiAAAAAgAAAEcA"
            "BAAQAAAAIQAAAAAAAABHAAQAEQAAAAYAAAAQAAAASAAEABIAAAAAAAAAGAAAAEgABQASAAAAAAAAACMAAAAAAA"
            "AARwADABIAAAACAAAARwAEABQAAAAiAAAAAgAAAEcABAAUAAAAIQAAAAEAAABHAAQAFQAAAAYAAAAQAAAASAAE"
            "ABYAAAAAAAAAGAAAAEgABQAWAAAAAAAAACMAAAAAAAAARwADABYAAAACAAAARwAEABgAAAAiAAAABAAAAEcABA"
            "AYAAAAIQAAAAAAAABHAAQAGQAAAAYAAAAQAAAASAAEABoAAAAAAAAAGAAAAEgABQAaAAAAAAAAACMAAAAAAAAA"
            "RwADABoAAAACAAAARwAEABwAAAAiAAAABAAAAEcABAAcAAAAIQAAAAEAAABHAAQAHQAAAAYAAAAQAAAASAAEAB"
            "4AAAAAAAAAGAAAAEgABQAeAAAAAAAAACMAAAAAAAAARwADAB4AAAACAAAARwAEACAAAAAiAAAABAAAAEcABAAg"
            "AAAAIQAAAAcAAAATAAIAAgAAACEAAwADAAAAAgAAABYAAwAGAAAAIAAAABcABAAHAAAABgAAAAQAAAAeAAMACA"
            "AAAAcAAAAdAAMACQAAAAgAAAAeAAMACgAAAAkAAAAgAAQACwAAAAwAAAAKAAAAOwAEAAsAAAAMAAAADAAAAB0A"
            "AwANAAAACAAAAB4AAwAOAAAADQAAACAABAAPAAAADAAAAA4AAAA7AAQADwAAABAAAAAMAAAAHQADABEAAAAIAA"
            "AAHgADABIAAAARAAAAIAAEABMAAAAMAAAAEgAAADsABAATAAAAFAAAAAwAAAAdAAMAFQAAAAgAAAAeAAMAFgAA"
            "ABUAAAAgAAQAFwAAAAwAAAAWAAAAOwAEABcAAAAYAAAADAAAAB0AAwAZAAAACAAAAB4AAwAaAAAAGQAAACAABA"
            "AbAAAADAAAABoAAAA7AAQAGwAAABwAAAAMAAAAHQADAB0AAAAIAAAAHgADAB4AAAAdAAAAIAAEAB8AAAAMAAAA"
            "HgAAADsABAAfAAAAIAAAAAwAAAA2AAUAAgAAAAQAAAAAAAAAAwAAAPgAAgAFAAAA/QABADgAAQA="),
        .kind       = AssetShaderKind_SpvVertex,
        .entryPoint = string_static("main"),
        .resources =
            {
                {AssetShaderResKind_StorageBuffer, .set = 0, .binding = 0},
                {AssetShaderResKind_StorageBuffer, .set = 2, .binding = 0},
                {AssetShaderResKind_StorageBuffer, .set = 2, .binding = 1},
                {AssetShaderResKind_StorageBuffer, .set = 4, .binding = 0},
                {AssetShaderResKind_StorageBuffer, .set = 4, .binding = 1},
                {AssetShaderResKind_StorageBuffer, .set = 4, .binding = 7},
            },
        .resourceCount = 6,
    },
    {
        .id         = string_static("3-specialization-constants_vertex_v1-3.spv"),
        .base64Data = string_static(
            "AwIjBwADAQAKAA0AGwAAAAAAAAARAAIAAQAAAAsABgABAAAAR0xTTC5zdGQuNDUwAAAAAA4AAwAAAAAAAQAAAA"
            "8ABgAAAAAABAAAAG1haW4AAAAADQAAAEgABQALAAAAAAAAAAsAAAAAAAAASAAFAAsAAAABAAAACwAAAAEAAABI"
            "AAUACwAAAAIAAAALAAAAAwAAAEgABQALAAAAAwAAAAsAAAAEAAAARwADAAsAAAACAAAARwAEABAAAAABAAAAAA"
            "AAAEcABAATAAAAAQAAAAMAAABHAAQAFwAAAAEAAAAHAAAAEwACAAIAAAAhAAMAAwAAAAIAAAAWAAMABgAAACAA"
            "AAAXAAQABwAAAAYAAAAEAAAAFQAEAAgAAAAgAAAAAAAAACsABAAIAAAACQAAAAEAAAAcAAQACgAAAAYAAAAJAA"
            "AAHgAGAAsAAAAHAAAABgAAAAoAAAAKAAAAIAAEAAwAAAADAAAACwAAADsABAAMAAAADQAAAAMAAAAVAAQADgAA"
            "ACAAAAABAAAAKwAEAA4AAAAPAAAAAAAAADIABAAOAAAAEAAAACoAAAAUAAIAEgAAADAAAwASAAAAEwAAACsABA"
            "AGAAAAFAAAAAAAAAArAAQABgAAABUAAAAAAIA/MgAEAAYAAAAXAAAARySnRCAABAAZAAAAAwAAAAcAAAA2AAUA"
            "AgAAAAQAAAAAAAAAAwAAAPgAAgAFAAAAbwAEAAYAAAARAAAAEAAAAKkABgAGAAAAFgAAABMAAAAVAAAAFAAAAF"
            "AABwAHAAAAGAAAABEAAAAWAAAAFwAAABUAAABBAAUAGQAAABoAAAANAAAADwAAAD4AAwAaAAAAGAAAAP0AAQA4"
            "AAEA"),
        .kind       = AssetShaderKind_SpvVertex,
        .entryPoint = string_static("main"),
        .specs =
            {
                {.type = AssetShaderType_i32, .binding = 0},
                {.type = AssetShaderType_bool, .binding = 3},
                {.type = AssetShaderType_f32, .binding = 7},
            },
        .specCount = 3,
    },
};

static const struct {
  String id;
  String text;
} g_errorTestData[] = {
    {
        .id   = string_static("invalid.spv"),
        .text = string_static("Hello World"),
    },
};

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(AssetView) { ecs_access_read(AssetShaderComp); }

ecs_module_init(loader_shader_spv_test_module) {
  ecs_register_view(ManagerView);
  ecs_register_view(AssetView);
}

spec(loader_shader_spv) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    asset_register(def);
    ecs_register_module(def, loader_shader_spv_test_module);

    world  = ecs_world_create(g_alloc_heap, def);
    runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_None);
  }

  it("can load SpirV shaders") {
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
      const AssetShaderComp* shader = ecs_utils_read_t(world, AssetView, asset, AssetShaderComp);
      check_eq_int(shader->kind, g_testData[i].kind);
      check_eq_string(shader->entryPoint, g_testData[i].entryPoint);
      check_eq_string(shader->data, records[i].data);

      check_require(shader->resources.count == g_testData[i].resourceCount);
      for (usize p = 0; p != g_testData[i].resourceCount; ++p) {
        check_eq_int(shader->resources.values[p].kind, g_testData[i].resources[p].kind);
        check_eq_int(shader->resources.values[p].set, g_testData[i].resources[p].set);
        check_eq_int(shader->resources.values[p].binding, g_testData[i].resources[p].binding);
      }

      check_require(shader->specs.count == g_testData[i].specCount);
      for (usize p = 0; p != g_testData[i].specCount; ++p) {
        check_eq_int(shader->specs.values[p].binding, g_testData[i].specs[p].binding);
        check_eq_int(shader->specs.values[p].type, g_testData[i].specs[p].type);
      }
    }

    array_for_t(records, AssetMemRecord, rec) { string_free(g_alloc_heap, rec->data); }
  }

  it("can unload SpirV shader assets") {
    const AssetMemRecord record = {
        .id   = string_lit("shader.spv"),
        .data = string_dup(g_alloc_heap, base64_decode_scratch(g_testData[0].base64Data)),
    };
    asset_manager_create_mem(world, AssetManagerFlags_None, &record, 1);
    ecs_world_flush(world);

    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
    const EcsEntityId asset   = asset_lookup(world, manager, string_lit("shader.spv"));

    asset_acquire(world, asset);
    asset_test_wait(runner);
    check(ecs_world_has_t(world, asset, AssetShaderComp));

    asset_release(world, asset);
    asset_test_wait(runner);
    check(!ecs_world_has_t(world, asset, AssetShaderComp));

    string_free(g_alloc_heap, record.data);
  }

  it("fails when loading invalid SpirV shader files") {
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
      check(!ecs_world_has_t(world, asset, AssetShaderComp));
    }
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
