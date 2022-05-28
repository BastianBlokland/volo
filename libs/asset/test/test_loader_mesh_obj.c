#include "asset.h"
#include "check_spec.h"
#include "core_alloc.h"
#include "core_array.h"
#include "ecs.h"

#include "utils_internal.h"

static const struct {
  String          id;
  String          text;
  AssetMeshVertex vertices[16];
  usize           vertexCount;
  AssetMeshIndex  indices[16];
  usize           indexCount;
} g_testData[] = {
    {
        .id   = string_static("vert_positions.obj"),
        .text = string_static("v 1.0 4.0 7.0 \n"
                              "v 2.0 5.0 8.0 \n"
                              "v 3.0 6.0 9.0 \n"
                              "f 1 2 3 \n"),
        .vertices =
            {
                {{1, 4, 7}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}},
                {{3, 6, 9}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}},
                {{2, 5, 8}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}},
            },
        .vertexCount = 3,
        .indices     = {0, 1, 2},
        .indexCount  = 3,
    },
    {
        .id   = string_static("vert_normals.obj"),
        .text = string_static("v 1.0 4.0 7.0\n"
                              "v 2.0 5.0 8.0\n"
                              "v 3.0 6.0 9.0\n"
                              "vn 1.0 0.0 0.0\n"
                              "vn 0.0 1.0 0.0\n"
                              "vn 0.0 0.0 1.0\n"
                              "f 1//1 2//2 3//3 \n"),
        .vertices =
            {
                {{1, 4, 7}, .normal = {1, 0, 0}, .tangent = {1, 0, 0, 1}},
                {{3, 6, 9}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}},
                {{2, 5, 8}, .normal = {0, 1, 0}, .tangent = {1, 0, 0, 1}},
            },
        .vertexCount = 3,
        .indices     = {0, 1, 2},
        .indexCount  = 3,
    },
    {
        .id   = string_static("vert_texcoords.obj"),
        .text = string_static("v 1.0 4.0 7.0\n"
                              "v 2.0 5.0 8.0\n"
                              "v 3.0 6.0 9.0\n"
                              "vt 0.1 0.5\n"
                              "vt 0.3 0.5\n"
                              "vt 0.5 0.5\n"
                              "f 1/1 2/2 3/3 \n"),
        .vertices =
            {
                {{1, 4, 7}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}, .texcoord = {0.1f, 0.5f}},
                {{3, 6, 9}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}, .texcoord = {0.5f, 0.5f}},
                {{2, 5, 8}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}, .texcoord = {0.3f, 0.5f}},
            },
        .vertexCount = 3,
        .indices     = {0, 1, 2},
        .indexCount  = 3,
    },
    {
        .id   = string_static("prefixed_face_elems.obj"),
        .text = string_static("v 1.0 4.0 7.0\n"
                              "v 2.0 5.0 8.0\n"
                              "v 3.0 6.0 9.0\n"
                              "vt 0.1 0.5\n"
                              "vt 0.3 0.5\n"
                              "vt 0.5 0.5\n"
                              "vn 1.0 0.0 0.0\n"
                              "vn 0.0 1.0 0.0\n"
                              "vn 0.0 0.0 1.0\n"
                              "f v1/vt1/vn-3 v2/vt2/vn-2 v3/vt3/vn-1\n"),
        .vertices =
            {
                {{1, 4, 7}, .normal = {1, 0, 0}, .tangent = {1, 0, 0, 1}, .texcoord = {0.1f, 0.5f}},
                {{3, 6, 9}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}, .texcoord = {0.5f, 0.5f}},
                {{2, 5, 8}, .normal = {0, 1, 0}, .tangent = {1, 0, 0, 1}, .texcoord = {0.3f, 0.5f}},
            },
        .vertexCount = 3,
        .indices     = {0, 1, 2},
        .indexCount  = 3,
    },
    {
        .id   = string_static("deduplicate_vertices.obj"),
        .text = string_static("v 1.0 4.0 7.0\n"
                              "v 2.0 5.0 8.0\n"
                              "v 3.0 6.0 9.0\n"
                              "v 1.0 4.0 7.0\n"
                              "v 2.0 5.0 8.0\n"
                              "v 3.0 6.0 9.0\n"
                              "f 1 2 3 \n"
                              "f 4 5 6 \n"
                              "f 1 2 3 \n"),
        .vertices =
            {
                {{1, 4, 7}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}},
                {{3, 6, 9}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}},
                {{2, 5, 8}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}},
            },
        .vertexCount = 3,
        .indices     = {0, 1, 2, 0, 1, 2, 0, 1, 2},
        .indexCount  = 9,
    },
    {
        .id   = string_static("triangulate.obj"),
        .text = string_static("v -0.5 -0.5 0.0 \n"
                              "v -0.5 0.5 0.0 \n"
                              "v 0.5 -0.5 0.0 \n"
                              "v 0.5 0.5 0.0 \n"
                              "f 1 2 3 4 \n"),
        .vertices =
            {
                {{-0.5, -0.5, 0}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}},
                {{+0.5, -0.5, 0}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}},
                {{-0.5, +0.5, 0}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}},
                {{+0.5, +0.5, 0}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}},
            },
        .vertexCount = 4,
        .indices     = {0, 1, 2, 0, 3, 1},
        .indexCount  = 6,
    },
    {
        .id   = string_static("negative_indices.obj"),
        .text = string_static("v 1.0 2.0 3.0 \n"
                              "v 4.0 5.0 6.0 \n"
                              "v 7.0 8.0 9.0 \n"
                              "f -3 -2 -1 \n"
                              "v 10.0 11.0 12.0 \n"
                              "v 13.0 14.0 15.0 \n"
                              "v 16.0 17.0 18.0 \n"
                              "f -1 -2 -3 \n"),
        .vertices =
            {
                {{1, 2, 3}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}},
                {{7, 8, 9}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}},
                {{4, 5, 6}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}},
                {{16, 17, 18}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}},
                {{10, 11, 12}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}},
                {{13, 14, 15}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}},
            },
        .vertexCount = 6,
        .indices     = {0, 1, 2, 3, 4, 5},
        .indexCount  = 6,
    },
    {
        .id   = string_static("comments.obj"),
        .text = string_static("# Hello World\n"
                              "v 1.0 4.0 7.0 \n"
                              "#Another comment\n"
                              "v 2.0 5.0 8.0 \n"
                              "#Another comment\n"
                              "#Another comment\n"
                              "v 3.0 6.0 9.0 \n"
                              "f 1 2 3 \n"
                              "# Comment at the end"),
        .vertices =
            {
                {{1, 4, 7}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}},
                {{3, 6, 9}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}},
                {{2, 5, 8}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}},
            },
        .vertexCount = 3,
        .indices     = {0, 1, 2},
        .indexCount  = 3,
    },
    {
        .id   = string_static("whitespace.obj"),
        .text = string_static("    v  \t  1.0  \t 4.0    7.0   \r\n"
                              "\tv\t2.0\t5.0\t8.0\n"
                              "\t\t v \t 3.0  6.0  9.0 \n"
                              "f\t 1  \t2  \t3 \r\n"),
        .vertices =
            {
                {{1, 4, 7}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}},
                {{3, 6, 9}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}},
                {{2, 5, 8}, .normal = {0, 0, 1}, .tangent = {1, 0, 0, 1}},
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
        .id   = string_static("positive-out-of-bounds-index.obj"),
        .text = string_static("v 1.0 4.0 7.0 \n"
                              "v 2.0 5.0 8.0 \n"
                              "v 3.0 6.0 9.0 \n"
                              "f 1 2 4 \n"),
    },
    {
        .id   = string_static("negative-out-of-bounds-index.obj"),
        .text = string_static("v 1.0 4.0 7.0 \n"
                              "v 2.0 5.0 8.0 \n"
                              "v 3.0 6.0 9.0 \n"
                              "f 1 2 -4 \n"),
    },
    {
        .id   = string_static("no-faces.obj"),
        .text = string_static("v -0.5 -0.5 0.0 \n"
                              "v 0.5 -0.5 0.0 \n"
                              "v -0.5 0.5 0.0 \n"
                              "v 0.5 0.5 0.0 \n"),
    },
    {
        .id   = string_static("invalid.obj"),
        .text = string_static("Hello World"),
    },
};

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(AssetView) { ecs_access_read(AssetMeshComp); }

ecs_module_init(loader_mesh_obj_test_module) {
  ecs_register_view(ManagerView);
  ecs_register_view(AssetView);
}

spec(loader_mesh_obj) {

  EcsDef*    def    = null;
  EcsWorld*  world  = null;
  EcsRunner* runner = null;

  setup() {
    def = ecs_def_create(g_alloc_heap);
    asset_register(def);
    ecs_register_module(def, loader_mesh_obj_test_module);

    world  = ecs_world_create(g_alloc_heap, def);
    runner = ecs_runner_create(g_alloc_heap, world, EcsRunnerFlags_None);
  }

  it("can load obj meshes") {
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
      const AssetMeshComp* mesh = ecs_utils_read_t(world, AssetView, asset, AssetMeshComp);

      // Verify the vertices.
      check_require(mesh->vertexCount == g_testData[i].vertexCount);
      for (usize j = 0; j != g_testData[i].vertexCount; ++j) {
        const AssetMeshVertex* vert         = &mesh->vertices[j];
        const AssetMeshVertex* vertExpected = &g_testData[i].vertices[j];
        check(geo_vector_equal(vert->position, vertExpected->position, 1e-6f));
        check(geo_vector_equal(vert->normal, vertExpected->normal, 1e-6f));
        check(geo_vector_equal(vert->tangent, vertExpected->tangent, 1e-6f));
        check(geo_vector_equal(vert->texcoord, vertExpected->texcoord, 1e-6f));
      }
      // Verify the indices.
      check_require(mesh->indexCount == g_testData[i].indexCount);
      for (usize j = 0; j != g_testData[i].indexCount; ++j) {
        check_eq_int(mesh->indices[j], g_testData[i].indices[j]);
      }
    };
  }

  it("can unload obj mesh assets") {
    const AssetMemRecord record = {.id = string_lit("mesh.obj"), .data = g_testData[0].text};
    asset_manager_create_mem(world, AssetManagerFlags_None, &record, 1);
    ecs_world_flush(world);

    AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
    const EcsEntityId asset   = asset_lookup(world, manager, string_lit("mesh.obj"));

    asset_acquire(world, asset);
    asset_test_wait(runner);
    check(ecs_world_has_t(world, asset, AssetMeshComp));

    asset_release(world, asset);
    asset_test_wait(runner);
    check(!ecs_world_has_t(world, asset, AssetMeshComp));
  }

  it("fails when loading invalid obj files") {
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
