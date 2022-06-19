#include "asset.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_base64.h"
#include "ecs.h"

#include "utils_internal.h"

static const struct {
  String          id, bufferId;
  String          text, bufferBase64;
  AssetMeshVertex vertices[16];
  usize           vertexCount;
  AssetMeshIndex  indices[16];
  usize           indexCount;
} g_testData[] = {
    {
        .id           = string_static("triangle.gltf"),
        .bufferId     = string_static("triangle.bin"),
        .bufferBase64 = string_static("AAAAAAAAAAAAAAAAAACAPwAAAAAAAAAAAAAAAAAAgD8AAAAA"),
        .text =
            string_static("{ \"scene\" : 0,"
                          "  \"scenes\" : [ { \"nodes\" : [ 0 ] } ],"
                          "  \"nodes\" : [ { \"mesh\" : 0 } ],"
                          "  \"meshes\" : [ {"
                          "    \"primitives\" : [ { \"attributes\" : { \"POSITION\" : 0 } } ] } ],"
                          "  \"buffers\" : [ { \"uri\" : \"triangle.bin\", \"byteLength\" : 36 } ],"
                          "  \"bufferViews\" : [ {"
                          "     \"buffer\" : 0, \"byteLength\" : 36, \"target\" : 34962 } ],"
                          "  \"accessors\" : [ {"
                          "      \"bufferView\" : 0,"
                          "      \"byteOffset\" : 0,"
                          "      \"componentType\" : 5126,"
                          "      \"count\" : 3,"
                          "      \"type\" : \"VEC3\","
                          "      \"max\" : [ 1.0, 1.0, 0.0 ],"
                          "      \"min\" : [ 0.0, 0.0, 0.0 ]"
                          "    }"
                          "  ],"
                          "  \"asset\" : { \"version\" : \"2.0\" }"
                          "}"),
        .vertices =
            {
                {.position = {0, 0, 0}, .normal = {0, 0, -1}, .tangent = {1, 0, 0, 1}},
                {.position = {1, 0, 0}, .normal = {0, 0, -1}, .tangent = {1, 0, 0, 1}},
                {.position = {0, 1, 0}, .normal = {0, 0, -1}, .tangent = {1, 0, 0, 1}},
            },
        .vertexCount = 3,
        .indices     = {0, 1, 2},
        .indexCount  = 3,
    },
    {
        .id       = string_static("triangle_indexed.gltf"),
        .bufferId = string_static("triangle_indexed.bin"),
        .bufferBase64 =
            string_static("AAABAAIAAAAAAAAAAAAAAAAAAAAAAIA/AAAAAAAAAAAAAAAAAACAPwAAAAA="),
        .text = string_static("{ \"scene\" : 0,"
                              "  \"scenes\" : [ { \"nodes\" : [ 0 ] } ],"
                              "  \"nodes\" : [ { \"mesh\" : 0 } ],"
                              "  \"meshes\" : [ {"
                              "    \"primitives\" : [ {"
                              "       \"attributes\" : { \"POSITION\" : 1 },"
                              "       \"indices\" : 0"
                              "     } ]"
                              "   } ],"
                              "  \"buffers\" : [ {"
                              "    \"uri\" : \"triangle_indexed.bin\","
                              "    \"byteLength\" : 44"
                              "  } ],"
                              "  \"bufferViews\" : ["
                              "    { \"buffer\" : 0, \"byteLength\" : 6 },"
                              "    { \"buffer\" : 0, \"byteOffset\" : 8, \"byteLength\" : 36  } ],"
                              "  \"accessors\" : [ {"
                              "    \"bufferView\" : 0,"
                              "    \"componentType\" : 5123,"
                              "    \"count\" : 3,"
                              "    \"type\" : \"SCALAR\""
                              "  }, {"
                              "    \"bufferView\" : 1,"
                              "    \"componentType\" : 5126,"
                              "    \"count\" : 3,"
                              "    \"type\" : \"VEC3\""
                              "  } ],"
                              "  \"asset\" : { \"version\" : \"2.0\" }"
                              "}"),
        .vertices =
            {
                {.position = {0, 0, 0}, .normal = {0, 0, -1}, .tangent = {1, 0, 0, 1}},
                {.position = {1, 0, 0}, .normal = {0, 0, -1}, .tangent = {1, 0, 0, 1}},
                {.position = {0, 1, 0}, .normal = {0, 0, -1}, .tangent = {1, 0, 0, 1}},
            },
        .vertexCount = 3,
        .indices     = {0, 1, 2},
        .indexCount  = 3,
    },
};

static const struct {
  String id;
  String text;
} g_errorTestData[] = {
    {
        .id   = string_static("invalid.gltf"),
        .text = string_static("Hello World"),
    },
};

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(AssetView) { ecs_access_read(AssetMeshComp); }

ecs_module_init(loader_mesh_gltf_test_module) {
  ecs_register_view(ManagerView);
  ecs_register_view(AssetView);
}

spec(loader_mesh_gltf) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    asset_register(def);
    ecs_register_module(def, loader_mesh_gltf_test_module);

    world  = ecs_world_create(g_alloc_heap, def);
    runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_None);
  }

  it("can load gltf meshes") {
    AssetMemRecord records[array_elems(g_testData) * 2];
    for (usize i = 0; i != array_elems(g_testData); ++i) {
      records[i * 2 + 0] = (AssetMemRecord){
          .id   = g_testData[i].id,
          .data = string_dup(g_alloc_heap, g_testData[i].text),
      };
      records[i * 2 + 1] = (AssetMemRecord){
          .id   = g_testData[i].bufferId,
          .data = string_dup(g_alloc_heap, base64_decode_scratch(g_testData[i].bufferBase64)),
      };
    }
    asset_manager_create_mem(world, AssetManagerFlags_None, records, array_elems(records));
    ecs_world_flush(world);

    for (usize i = 0; i != array_elems(g_testData); ++i) {
      AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
      const EcsEntityId asset   = asset_lookup(world, manager, records[i * 2].id);
      asset_acquire(world, asset);

      asset_test_wait(runner);

      check_require(ecs_world_has_t(world, asset, AssetLoadedComp));
      const AssetMeshComp* mesh = ecs_utils_read_t(world, AssetView, asset, AssetMeshComp);

      // Verify the vertices.
      check_require(mesh->vertexCount == g_testData[i].vertexCount);
      for (usize j = 0; j != g_testData[i].vertexCount; ++j) {
        const AssetMeshVertex* vert         = &mesh->vertexData[j];
        const AssetMeshVertex* vertExpected = &g_testData[i].vertices[j];
        check(geo_vector_equal(vert->position, vertExpected->position, 1e-6f));
        check(geo_vector_equal(vert->normal, vertExpected->normal, 1e-6f));
        check(geo_vector_equal(vert->tangent, vertExpected->tangent, 1e-6f));
        check(geo_vector_equal(vert->texcoord, vertExpected->texcoord, 1e-6f));
      }
      // Verify the indices.
      check_require(mesh->indexCount == g_testData[i].indexCount);
      for (usize j = 0; j != g_testData[i].indexCount; ++j) {
        check_eq_int(mesh->indexData[j], g_testData[i].indices[j]);
      }
    };

    array_for_t(records, AssetMemRecord, rec) { string_free(g_alloc_heap, rec->data); }
  }

  it("can unload gltf mesh assets") {
    const AssetMemRecord records[] = {
        {
            .id   = g_testData[0].id,
            .data = string_dup(g_alloc_heap, g_testData[0].text),
        },
        {
            .id   = g_testData[0].bufferId,
            .data = string_dup(g_alloc_heap, base64_decode_scratch(g_testData[0].bufferBase64)),
        },
    };
    asset_manager_create_mem(world, AssetManagerFlags_None, records, array_elems(records));
    ecs_world_flush(world);

    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
    const EcsEntityId asset   = asset_lookup(world, manager, g_testData[0].id);

    asset_acquire(world, asset);
    asset_test_wait(runner);
    check(ecs_world_has_t(world, asset, AssetMeshComp));

    asset_release(world, asset);
    asset_test_wait(runner);
    check(!ecs_world_has_t(world, asset, AssetMeshComp));

    array_for_t(records, AssetMemRecord, rec) { string_free(g_alloc_heap, rec->data); }
  }

  it("fails when loading invalid gltf files") {
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
      check(!ecs_world_has_t(world, asset, AssetMeshComp));
    }
  }

  teardown() {
    ecs_runner_destroy(runner);
    ecs_world_destroy(world);
    ecs_def_destroy(def);
  }
}
