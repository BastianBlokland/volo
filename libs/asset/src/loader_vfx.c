#include "asset_vfx.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_thread.h"
#include "core_time.h"
#include "data.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "manager_internal.h"
#include "repo_internal.h"

static DataReg* g_dataReg;
static DataMeta g_dataVfxDefMeta;

typedef struct {
  f32 x, y;
} VfxVec2Def;

typedef struct {
  f32 x, y, z;
} VfxVec3Def;

typedef struct {
  f32 x, y, z;
} VfxRotDef;

typedef struct {
  f32 r, g, b, a;
} VfxColorDef;

typedef struct {
  String         atlasEntry;
  u32            flipbookCount;
  f32            flipbookTime;
  VfxVec3Def     position;
  VfxRotDef      rotation;
  VfxVec2Def     size;
  f32            scaleInTime, scaleOutTime;
  VfxColorDef*   color;
  f32            fadeInTime, fadeOutTime;
  AssetVfxBlend  blend;
  AssetVfxFacing facing;
  u32            count;
  f32            interval, lifetime;
} VfxEmitterDef;

typedef struct {
  struct {
    VfxEmitterDef* values;
    usize          count;
  } emitters;
} VfxDef;

static void vfx_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    g_dataReg = data_reg_create(g_alloc_persist);

    // clang-format off
    data_reg_struct_t(g_dataReg, VfxVec2Def);
    data_reg_field_t(g_dataReg, VfxVec2Def, x, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, VfxVec2Def, y, data_prim_t(f32), .flags = DataFlags_Opt);

    data_reg_struct_t(g_dataReg, VfxVec3Def);
    data_reg_field_t(g_dataReg, VfxVec3Def, x, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, VfxVec3Def, y, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, VfxVec3Def, z, data_prim_t(f32), .flags = DataFlags_Opt);

    data_reg_struct_t(g_dataReg, VfxRotDef);
    data_reg_field_t(g_dataReg, VfxRotDef, x, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, VfxRotDef, y, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, VfxRotDef, z, data_prim_t(f32), .flags = DataFlags_Opt);

    data_reg_struct_t(g_dataReg, VfxColorDef);
    data_reg_field_t(g_dataReg, VfxColorDef, r, data_prim_t(f32));
    data_reg_field_t(g_dataReg, VfxColorDef, g, data_prim_t(f32));
    data_reg_field_t(g_dataReg, VfxColorDef, b, data_prim_t(f32));
    data_reg_field_t(g_dataReg, VfxColorDef, a, data_prim_t(f32));

    data_reg_enum_t(g_dataReg, AssetVfxBlend);
    data_reg_const_t(g_dataReg, AssetVfxBlend, None);
    data_reg_const_t(g_dataReg, AssetVfxBlend, Alpha);
    data_reg_const_t(g_dataReg, AssetVfxBlend, Additive);
    data_reg_const_t(g_dataReg, AssetVfxBlend, AdditiveDouble);
    data_reg_const_t(g_dataReg, AssetVfxBlend, AdditiveQuad);

    data_reg_enum_t(g_dataReg, AssetVfxFacing);
    data_reg_const_t(g_dataReg, AssetVfxFacing, World);
    data_reg_const_t(g_dataReg, AssetVfxFacing, BillboardSphere);
    data_reg_const_t(g_dataReg, AssetVfxFacing, BillboardCylinder);

    data_reg_struct_t(g_dataReg, VfxEmitterDef);
    data_reg_field_t(g_dataReg, VfxEmitterDef, atlasEntry, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, VfxEmitterDef, flipbookCount, data_prim_t(u32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, VfxEmitterDef, flipbookTime, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, VfxEmitterDef, position, t_VfxVec3Def, .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, VfxEmitterDef, rotation, t_VfxRotDef, .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, VfxEmitterDef, size, t_VfxVec2Def);
    data_reg_field_t(g_dataReg, VfxEmitterDef, scaleInTime, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, VfxEmitterDef, scaleOutTime, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, VfxEmitterDef, color, t_VfxColorDef, .container = DataContainer_Pointer, .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, VfxEmitterDef, fadeInTime, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, VfxEmitterDef, fadeOutTime, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, VfxEmitterDef, blend, t_AssetVfxBlend, .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, VfxEmitterDef, facing, t_AssetVfxFacing, .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, VfxEmitterDef, count, data_prim_t(u32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, VfxEmitterDef, interval, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, VfxEmitterDef, lifetime, data_prim_t(f32), .flags = DataFlags_Opt);

    data_reg_struct_t(g_dataReg, VfxDef);
    data_reg_field_t(g_dataReg, VfxDef, emitters, t_VfxEmitterDef, .container = DataContainer_Array);
    // clang-format on

    g_dataVfxDefMeta = data_meta_t(t_VfxDef);
  }
  thread_spinlock_unlock(&g_initLock);
}

typedef enum {
  VfxError_None            = 0,
  VfxError_TooManyEmitters = 1,

  VfxError_Count,
} VfxError;

static String vfx_error_str(const VfxError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Vfx specifies more emitters then supported"),
  };
  ASSERT(array_elems(g_msgs) == VfxError_Count, "Incorrect number of vfx-error messages");
  return g_msgs[err];
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

static GeoVector vfx_build_vec3(const VfxVec3Def* def) {
  return geo_vector(def->x, def->y, def->z);
}

static GeoQuat vfx_build_rot(const VfxRotDef* def) {
  const GeoVector eulerAnglesDeg = geo_vector(def->x, def->y, def->z);
  return geo_quat_from_euler(geo_vector_mul(eulerAnglesDeg, math_deg_to_rad));
}

static GeoColor vfx_build_color(const VfxColorDef* def) {
  return geo_color(def->r, def->g, def->b, def->a);
}

static void vfx_build_emitter(const VfxEmitterDef* def, AssetVfxEmitter* out) {
  out->atlasEntry    = string_hash(def->atlasEntry);
  out->flipbookCount = math_max(1, def->flipbookCount);
  out->flipbookTime  = math_max(time_millisecond, (TimeDuration)time_seconds(def->flipbookTime));
  out->position      = vfx_build_vec3(&def->position);
  out->rotation      = vfx_build_rot(&def->rotation);
  out->sizeX         = def->size.x;
  out->sizeY         = def->size.y;
  out->scaleInTime   = (TimeDuration)time_seconds(def->scaleInTime);
  out->scaleOutTime  = (TimeDuration)time_seconds(def->scaleOutTime);
  out->color         = def->color ? vfx_build_color(def->color) : geo_color_white;
  out->fadeInTime    = (TimeDuration)time_seconds(def->fadeInTime);
  out->fadeOutTime   = (TimeDuration)time_seconds(def->fadeOutTime);
  out->blend         = def->blend;
  out->facing        = def->facing;
  out->count         = math_max(1, def->count);
  out->interval      = (TimeDuration)time_seconds(def->interval);
  out->lifetime      = def->lifetime > 0 ? (TimeDuration)time_seconds(def->lifetime) : i64_max;
}

static void vfx_build_def(const VfxDef* def, AssetVfxComp* out) {
  diag_assert(def->emitters.count <= asset_vfx_max_emitters);

  out->emitterCount = (u32)def->emitters.count;
  for (u32 i = 0; i != out->emitterCount; ++i) {
    vfx_build_emitter(&def->emitters.values[i], &out->emitters[i]);
  }
}

ecs_module_init(asset_vfx_module) {
  vfx_datareg_init();

  ecs_register_comp(AssetVfxComp);

  ecs_register_view(VfxUnloadView);

  ecs_register_system(VfxUnloadAssetSys, ecs_view_id(VfxUnloadView));
}

void asset_load_vfx(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;

  VfxDef         vfxDef;
  String         errMsg;
  DataReadResult readRes;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataVfxDefMeta, mem_var(vfxDef), &readRes);
  if (UNLIKELY(readRes.error)) {
    errMsg = readRes.errorMsg;
    goto Error;
  }
  if (UNLIKELY(vfxDef.emitters.count > asset_vfx_max_emitters)) {
    errMsg = vfx_error_str(VfxError_TooManyEmitters);
    goto Error;
  }

  AssetVfxComp* result = ecs_world_add_t(world, entity, AssetVfxComp);
  vfx_build_def(&vfxDef, result);

  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  goto Cleanup;

Error:
  log_e("Failed to load Vfx", log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);

Cleanup:
  data_destroy(g_dataReg, g_alloc_heap, g_dataVfxDefMeta, mem_var(vfxDef));
  asset_repo_source_close(src);
}
