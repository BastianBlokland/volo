#include "asset_vfx.h"
#include "core_alloc.h"
#include "core_math.h"
#include "core_thread.h"
#include "data.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "manager_internal.h"
#include "repo_internal.h"

static DataReg* g_dataReg;
static DataMeta g_dataVfxDefMeta;

typedef struct {
  f32 x, y;
} AssetVfxVec2Def;

typedef struct {
  f32 x, y, z;
} AssetVfxVec3Def;

typedef struct {
  f32 x, y, z;
} AssetVfxRotDef;

typedef struct {
  f32 r, g, b, a;
} AssetVfxColorDef;

typedef struct {
  String           atlasEntry;
  AssetVfxVec3Def  position;
  AssetVfxRotDef   rotation;
  AssetVfxVec2Def  size;
  AssetVfxColorDef color;
} AssetVfxDef;

static void vfx_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    g_dataReg = data_reg_create(g_alloc_persist);

    // clang-format off
    data_reg_struct_t(g_dataReg, AssetVfxVec2Def);
    data_reg_field_t(g_dataReg, AssetVfxVec2Def, x, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetVfxVec2Def, y, data_prim_t(f32));

    data_reg_struct_t(g_dataReg, AssetVfxVec3Def);
    data_reg_field_t(g_dataReg, AssetVfxVec3Def, x, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetVfxVec3Def, y, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetVfxVec3Def, z, data_prim_t(f32));

    data_reg_struct_t(g_dataReg, AssetVfxRotDef);
    data_reg_field_t(g_dataReg, AssetVfxRotDef, x, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetVfxRotDef, y, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetVfxRotDef, z, data_prim_t(f32));

    data_reg_struct_t(g_dataReg, AssetVfxColorDef);
    data_reg_field_t(g_dataReg, AssetVfxColorDef, r, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetVfxColorDef, g, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetVfxColorDef, b, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetVfxColorDef, a, data_prim_t(f32));

    data_reg_struct_t(g_dataReg, AssetVfxDef);
    data_reg_field_t(g_dataReg, AssetVfxDef, atlasEntry, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, AssetVfxDef, position, t_AssetVfxVec3Def, .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, AssetVfxDef, rotation, t_AssetVfxRotDef, .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, AssetVfxDef, size, t_AssetVfxVec2Def);
    data_reg_field_t(g_dataReg, AssetVfxDef, color, t_AssetVfxColorDef);
    // clang-format on

    g_dataVfxDefMeta = data_meta_t(t_AssetVfxDef);
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define_public(AssetVfxComp);

ecs_view_define(VfxUnloadView) {
  ecs_access_with(AssetVfxComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any vfx-asset components for unloaded assets.
 */
ecs_system_define(VfxUnloadAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, VfxUnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetVfxComp);
  }
}

static GeoVector asset_vfx_build_vec3(const AssetVfxVec3Def* def) {
  return geo_vector(def->x, def->y, def->z);
}

static GeoQuat asset_vfx_build_rot(const AssetVfxRotDef* def) {
  const GeoVector eulerAnglesDeg = geo_vector(def->x, def->y, def->z);
  return geo_quat_from_euler(geo_vector_mul(eulerAnglesDeg, math_deg_to_rad));
}

static GeoColor asset_vfx_build_color(const AssetVfxColorDef* def) {
  return geo_color(def->r, def->g, def->b, def->a);
}

static void asset_vfx_build(const AssetVfxDef* def, AssetVfxComp* out) {
  out->atlasEntry = string_hash(def->atlasEntry);
  out->position   = asset_vfx_build_vec3(&def->position);
  out->rotation   = asset_vfx_build_rot(&def->rotation);
  out->sizeX      = def->size.x;
  out->sizeY      = def->size.y;
  out->color      = asset_vfx_build_color(&def->color);
}

ecs_module_init(asset_vfx_module) {
  vfx_datareg_init();

  ecs_register_comp(AssetVfxComp);

  ecs_register_view(VfxUnloadView);

  ecs_register_system(VfxUnloadAssetSys, ecs_view_id(VfxUnloadView));
}

void asset_load_vfx(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;

  AssetVfxDef    vfxDef;
  String         errMsg;
  DataReadResult readRes;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataVfxDefMeta, mem_var(vfxDef), &readRes);
  if (UNLIKELY(readRes.error)) {
    errMsg = readRes.errorMsg;
    goto Error;
  }

  AssetVfxComp* result = ecs_world_add_t(world, entity, AssetVfxComp);
  asset_vfx_build(&vfxDef, result);

  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  goto Cleanup;

Error:
  log_e("Failed to load Vfx", log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);

Cleanup:
  data_destroy(g_dataReg, g_alloc_heap, g_dataVfxDefMeta, mem_var(vfxDef));
  asset_repo_source_close(src);
}
