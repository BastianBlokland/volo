#include "asset_mesh.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_thread.h"
#include "data.h"
#include "ecs_world.h"
#include "geo_matrix.h"
#include "log_logger.h"

#include "mesh_utils_internal.h"
#include "repo_internal.h"

/**
 * ProceduralMEsh - Procedurally generated mesh.
 */

#define pme_max_verts (1024 * 100)
#define pme_max_subdivisions 100

static DataReg* g_dataReg;
static DataMeta g_dataPmeDefMeta;

typedef enum {
  PmeAxis_Up,
  PmeAxis_Down,
  PmeAxis_Right,
  PmeAxis_Left,
  PmeAxis_Forward,
  PmeAxis_Backward,
} PmeAxis;

typedef enum {
  PmeType_Triangle,
  PmeType_Quad,
  PmeType_Cube,
} PmeType;

typedef struct {
  PmeType type;
  PmeAxis axis;
  u32     subdivisions;
  f32     scaleX, scaleY, scaleZ;
  f32     offsetX, offsetY, offsetZ;
} PmeDef;

static void pme_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    g_dataReg = data_reg_create(g_alloc_persist);

    // clang-format off
    data_reg_enum_t(g_dataReg, PmeType);
    data_reg_const_t(g_dataReg, PmeType, Triangle);
    data_reg_const_t(g_dataReg, PmeType, Quad);
    data_reg_const_t(g_dataReg, PmeType, Cube);

    data_reg_enum_t(g_dataReg, PmeAxis);
    data_reg_const_t(g_dataReg, PmeAxis, Up);
    data_reg_const_t(g_dataReg, PmeAxis, Down);
    data_reg_const_t(g_dataReg, PmeAxis, Right);
    data_reg_const_t(g_dataReg, PmeAxis, Left);
    data_reg_const_t(g_dataReg, PmeAxis, Forward);
    data_reg_const_t(g_dataReg, PmeAxis, Backward);

    data_reg_struct_t(g_dataReg, PmeDef);
    data_reg_field_t(g_dataReg, PmeDef, type, t_PmeType);
    data_reg_field_t(g_dataReg, PmeDef, axis, t_PmeAxis);
    data_reg_field_t(g_dataReg, PmeDef, subdivisions, data_prim_t(u32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, PmeDef, scaleX, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, PmeDef, scaleY, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, PmeDef, scaleZ, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, PmeDef, offsetX, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, PmeDef, offsetY, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, PmeDef, offsetZ, data_prim_t(f32), .flags = DataFlags_Opt);
    // clang-format on

    g_dataPmeDefMeta = data_meta_t(t_PmeDef);
  }
  thread_spinlock_unlock(&g_initLock);
}

typedef struct {
  const PmeDef*     def;
  AssetMeshBuilder* builder;
  GeoMatrix         transformGlobal, transformLocal;
} PmeGenerator;

static GeoVector pme_def_normal(const PmeDef* def) {
  switch (def->axis) {
  case PmeAxis_Up:
    return geo_down;
  case PmeAxis_Down:
    return geo_up;
  case PmeAxis_Right:
    return geo_left;
  case PmeAxis_Left:
    return geo_right;
  case PmeAxis_Forward:
    return geo_backward;
  case PmeAxis_Backward:
    return geo_forward;
  }
  diag_crash();
}

static GeoMatrix pme_def_matrix(const PmeDef* def) {
  const GeoMatrix t = geo_matrix_translate(geo_vector(def->offsetX, def->offsetY, def->offsetZ));
  const GeoMatrix r = geo_matrix_rotate_look(pme_def_normal(def), geo_up);
  const GeoMatrix s = geo_matrix_scale(geo_vector(
      def->scaleX != 0.0f ? def->scaleX : 1.0f,
      def->scaleY != 0.0f ? def->scaleY : 1.0f,
      def->scaleZ != 0.0f ? def->scaleZ : 1.0f));

  const GeoMatrix ts = geo_matrix_mul(&t, &s);
  return geo_matrix_mul(&ts, &r);
}

static GeoVector pme_transform(PmeGenerator* gen, GeoVector vec) {
  const GeoMatrix mat = geo_matrix_mul(&gen->transformGlobal, &gen->transformLocal);
  return geo_matrix_transform3(&mat, vec);
}

static void pme_push_vert(PmeGenerator* gen, const GeoVector pos, const GeoVector texcoord) {
  asset_mesh_builder_push(
      gen->builder,
      (AssetMeshVertex){
          .position = pme_transform(gen, pos),
          .texcoord = texcoord,
      });
}

void pme_push_triangle(PmeGenerator* gen) {
  /**
   * Subdivided triangle.
   *
   *    /\
   *   /\/\
   *  /\/\/\
   * /\/\/\/\
   *
   */
  const u32 numSteps = gen->def->subdivisions + 1;
  const f32 step     = 1.0f / numSteps;
  for (u32 y = numSteps; y-- != 0;) {
    for (u32 x = 0; x != (numSteps - y); ++x) {
      const f32 xMin = (x + y * 0.5f + 0.0f) * step;
      const f32 xMid = (x + y * 0.5f + 0.5f) * step;
      const f32 xMax = (x + y * 0.5f + 1.0f) * step;
      const f32 yMin = (y + 0.0f) * step;
      const f32 yMax = (y + 1.0f) * step;

      pme_push_vert(gen, geo_vector(xMin - 0.5f, yMin - 0.5f), geo_vector(xMin, yMin));
      pme_push_vert(gen, geo_vector(xMid - 0.5f, yMax - 0.5f), geo_vector(xMid, yMax));
      pme_push_vert(gen, geo_vector(xMax - 0.5f, yMin - 0.5f), geo_vector(xMax, yMin));

      if (y) {
        /**
         * Fill in the hole in the row below us.
         */
        const f32 yLastRow = yMin - step;
        pme_push_vert(gen, geo_vector(xMin - 0.5f, yMin - 0.5f), geo_vector(xMin, yMin));
        pme_push_vert(gen, geo_vector(xMax - 0.5f, yMin - 0.5f), geo_vector(xMax, yMin));
        pme_push_vert(gen, geo_vector(xMid - 0.5f, yLastRow - 0.5f), geo_vector(xMid, yLastRow));
      }
    }
  }
}

void pme_push_quad(PmeGenerator* gen) {
  /**
   * Subdivided quad.
   */
  const u32 numSteps = gen->def->subdivisions + 1;
  const f32 step     = 1.0f / numSteps;
  for (u32 y = 0; y != numSteps; ++y) {
    for (u32 x = 0; x != numSteps; ++x) {
      const f32 xMin = x * step;
      const f32 yMin = y * step;
      const f32 xMax = xMin + step;
      const f32 yMax = yMin + step;

      pme_push_vert(gen, geo_vector(xMin - 0.5f, yMax - 0.5f), geo_vector(xMin, yMax));
      pme_push_vert(gen, geo_vector(xMax - 0.5f, yMax - 0.5f), geo_vector(xMax, yMax));
      pme_push_vert(gen, geo_vector(xMin - 0.5f, yMin - 0.5f), geo_vector(xMin, yMin));
      pme_push_vert(gen, geo_vector(xMax - 0.5f, yMax - 0.5f), geo_vector(xMax, yMax));
      pme_push_vert(gen, geo_vector(xMax - 0.5f, yMin - 0.5f), geo_vector(xMax, yMin));
      pme_push_vert(gen, geo_vector(xMin - 0.5f, yMin - 0.5f), geo_vector(xMin, yMin));
    }
  }
}

static void pme_generate_triangle(PmeGenerator* gen) {
  pme_push_triangle(gen);
  asset_mesh_compute_flat_normals(gen->builder);
  asset_mesh_compute_tangents(gen->builder);
}

static void pme_generate_quad(PmeGenerator* gen) {
  pme_push_quad(gen);
  asset_mesh_compute_flat_normals(gen->builder);
  asset_mesh_compute_tangents(gen->builder);
}

static void pme_generate_cube(PmeGenerator* gen) {
  const GeoMatrix faceRotations[] = {
      geo_matrix_rotate_look(geo_up, geo_forward),
      geo_matrix_rotate_look(geo_down, geo_forward),
      geo_matrix_rotate_look(geo_right, geo_up),
      geo_matrix_rotate_look(geo_left, geo_up),
      geo_matrix_rotate_look(geo_forward, geo_up),
      geo_matrix_rotate_look(geo_backward, geo_up),
  };
  array_for_t(faceRotations, GeoMatrix, rotMat) {
    const GeoVector offset = geo_vector_mul(geo_matrix_transform3(rotMat, geo_backward), 0.5f);
    const GeoMatrix t      = geo_matrix_translate(offset);
    gen->transformLocal    = geo_matrix_mul(&t, rotMat);
    pme_push_quad(gen);
  }
  asset_mesh_compute_flat_normals(gen->builder);
  asset_mesh_compute_tangents(gen->builder);
}

static void pme_generate(PmeGenerator* gen) {
  switch (gen->def->type) {
  case PmeType_Triangle:
    pme_generate_triangle(gen);
    break;
  case PmeType_Quad:
    pme_generate_quad(gen);
    break;
  case PmeType_Cube:
    pme_generate_cube(gen);
    break;
  }
}

typedef enum {
  PmeError_None = 0,
  PmeError_TooManySubdivisions,

  PmeError_Count,
} PmeError;

static String pme_error_str(const PmeError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Pme specifies more subdivisions then are supported"),
  };
  ASSERT(array_elems(g_msgs) == PmeError_Count, "Incorrect number of pme-error messages");
  return g_msgs[err];
}

void asset_load_pme(EcsWorld* world, const EcsEntityId entity, AssetSource* src) {
  pme_datareg_init();

  String            errMsg;
  AssetMeshBuilder* builder = null;
  PmeDef            def;
  DataReadResult    result;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataPmeDefMeta, mem_var(def), &result);

  if (UNLIKELY(result.error)) {
    errMsg = result.errorMsg;
    goto Error;
  }
  if (UNLIKELY(def.subdivisions > pme_max_subdivisions)) {
    errMsg = pme_error_str(PmeError_TooManySubdivisions);
    goto Error;
  }

  builder = asset_mesh_builder_create(g_alloc_heap, pme_max_verts);
  pme_generate(&(PmeGenerator){
      .def             = &def,
      .builder         = builder,
      .transformGlobal = pme_def_matrix(&def),
      .transformLocal  = geo_matrix_ident(),
  });

  *ecs_world_add_t(world, entity, AssetMeshComp) = asset_mesh_create(builder);
  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  goto Done;

Error:
  log_e("Failed to load pme mesh", log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);

Done:
  if (builder) {
    asset_mesh_builder_destroy(builder);
  }
  asset_repo_source_close(src);
}
