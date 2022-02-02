#include "asset_mesh.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_thread.h"
#include "data.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "mesh_utils_internal.h"
#include "repo_internal.h"

/**
 * ProceduralMEsh - Procedurally generated mesh.
 */

#define pme_max_verts 512

static DataReg* g_dataReg;
static DataMeta g_dataPmeDefMeta;

typedef enum {
  PmeType_Triangle,
} PmeType;

typedef struct {
  PmeType type;
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

    data_reg_struct_t(g_dataReg, PmeDef);
    data_reg_field_t(g_dataReg, PmeDef, type, t_PmeType);
    // clang-format on

    g_dataPmeDefMeta = data_meta_t(t_PmeDef);
  }
  thread_spinlock_unlock(&g_initLock);
}

static void pme_generate_triangle(const PmeDef* def, AssetMeshBuilder* builder) {
  (void)def;
  asset_mesh_builder_push(
      builder,
      (AssetMeshVertex){
          .position = geo_vector(-0.5, -0.5),
          .normal   = geo_backward,
          .tangent  = geo_vector(0, 1, 0, -1),
          .texcoord = geo_vector(0, 0),
      });
  asset_mesh_builder_push(
      builder,
      (AssetMeshVertex){
          .position = geo_vector(0, 0.5),
          .normal   = geo_backward,
          .tangent  = geo_vector(0, 1, 0, -1),
          .texcoord = geo_vector(0.5, 1),
      });
  asset_mesh_builder_push(
      builder,
      (AssetMeshVertex){
          .position = geo_vector(0.5, -0.5),
          .normal   = geo_backward,
          .tangent  = geo_vector(0, 1, 0, -1),
          .texcoord = geo_vector(1.0, 0.0),
      });
}

static void pme_generate(const PmeDef* def, AssetMeshBuilder* builder) {
  switch (def->type) {
  case PmeType_Triangle:
    pme_generate_triangle(def, builder);
    break;
  }
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

  builder = asset_mesh_builder_create(g_alloc_heap, pme_max_verts);
  pme_generate(&def, builder);

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
