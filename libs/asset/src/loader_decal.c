#include "asset_decal.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_float.h"
#include "core_math.h"
#include "core_thread.h"
#include "data.h"
#include "data_schema.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "repo_internal.h"

#define decal_default_size 1.0f
#define decal_default_thickness 0.25f
#define decal_default_spacing 1.0f

static DataReg* g_dataReg;
static DataMeta g_dataDecalDefMeta;

typedef struct {
  AssetDecalMask* values;
  usize           count;
} DecalMaskDef;

typedef struct {
  bool             trail;
  f32              spacing;
  AssetDecalAxis   projectionAxis;
  String           colorAtlasEntry;
  String           normalAtlasEntry; // Optional, empty if unused.
  AssetDecalNormal baseNormal;
  bool             fadeUsingDepthNormal;
  bool             noColorOutput;
  bool             randomRotation;
  bool             snapToTerrain;
  DecalMaskDef     excludeMask;
  f32              roughness;
  f32              alphaMin, alphaMax;
  f32              width, height;
  f32              thickness;
  f32              scaleMin, scaleMax;
  f32              fadeInTime, fadeOutTime;
} DecalDef;

static void decal_datareg_init(void) {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    DataReg* reg = data_reg_create(g_allocPersist);

    // clang-format off
    data_reg_enum_t(reg, AssetDecalAxis);
    data_reg_const_t(reg, AssetDecalAxis, LocalY);
    data_reg_const_t(reg, AssetDecalAxis, LocalZ);
    data_reg_const_t(reg, AssetDecalAxis, WorldY);

    data_reg_enum_t(reg, AssetDecalNormal);
    data_reg_const_t(reg, AssetDecalNormal, GBuffer);
    data_reg_const_t(reg, AssetDecalNormal, DepthBuffer);
    data_reg_const_t(reg, AssetDecalNormal, DecalTransform);

    data_reg_enum_t(reg, AssetDecalMask);
    data_reg_const_t(reg, AssetDecalMask, Geometry);
    data_reg_const_t(reg, AssetDecalMask, Terrain);
    data_reg_const_t(reg, AssetDecalMask, Unit);

    data_reg_struct_t(reg, DecalDef);
    data_reg_field_t(reg, DecalDef, trail, data_prim_t(bool), .flags = DataFlags_Opt);
    data_reg_field_t(reg, DecalDef, spacing, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, DecalDef, projectionAxis, t_AssetDecalAxis);
    data_reg_field_t(reg, DecalDef, colorAtlasEntry, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, DecalDef, normalAtlasEntry, data_prim_t(String), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, DecalDef, baseNormal, t_AssetDecalNormal, .flags = DataFlags_Opt);
    data_reg_field_t(reg, DecalDef, fadeUsingDepthNormal, data_prim_t(bool), .flags = DataFlags_Opt);
    data_reg_field_t(reg, DecalDef, noColorOutput, data_prim_t(bool), .flags = DataFlags_Opt);
    data_reg_field_t(reg, DecalDef, randomRotation, data_prim_t(bool), .flags = DataFlags_Opt);
    data_reg_field_t(reg, DecalDef, snapToTerrain, data_prim_t(bool), .flags = DataFlags_Opt);
    data_reg_field_t(reg, DecalDef, excludeMask, t_AssetDecalMask, .container = DataContainer_Array, .flags = DataFlags_Opt);
    data_reg_field_t(reg, DecalDef, roughness, data_prim_t(f32));
    data_reg_field_t(reg, DecalDef, alphaMin, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, DecalDef, alphaMax, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, DecalDef, width, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, DecalDef, height, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, DecalDef, thickness, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, DecalDef, scaleMin, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, DecalDef, scaleMax, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, DecalDef, fadeInTime, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, DecalDef, fadeOutTime, data_prim_t(f32), .flags = DataFlags_Opt);
    // clang-format on

    g_dataDecalDefMeta = data_meta_t(t_DecalDef);
    g_dataReg          = reg;
  }
  thread_spinlock_unlock(&g_initLock);
}

ecs_comp_define_public(AssetDecalComp);

ecs_view_define(DecalUnloadView) {
  ecs_access_with(AssetDecalComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any decal-asset components for unloaded assets.
 */
ecs_system_define(DecalUnloadAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, DecalUnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetDecalComp);
  }
}

static AssetDecalMask decal_build_mask(const DecalMaskDef* def) {
  AssetDecalMask mask = 0;
  array_ptr_for_t(*def, AssetDecalMask, val) { mask |= *val; }
  return mask;
}

static AssetDecalFlags decal_build_flags(const DecalDef* def) {
  AssetDecalFlags flags = 0;
  flags |= def->trail ? AssetDecalFlags_Trail : 0;
  flags |= !def->noColorOutput ? AssetDecalFlags_OutputColor : 0;
  flags |= def->fadeUsingDepthNormal ? AssetDecalFlags_FadeUsingDepthNormal : 0;
  flags |= def->randomRotation ? AssetDecalFlags_RandomRotation : 0;
  flags |= def->snapToTerrain ? AssetDecalFlags_SnapToTerrain : 0;
  return flags;
}

static void decal_build_def(const DecalDef* def, AssetDecalComp* out) {
  out->spacing          = def->spacing < f32_epsilon ? decal_default_spacing : def->spacing;
  out->projectionAxis   = def->projectionAxis;
  out->atlasColorEntry  = string_hash(def->colorAtlasEntry);
  out->atlasNormalEntry = string_maybe_hash(def->normalAtlasEntry);
  out->baseNormal       = def->baseNormal;
  out->flags            = decal_build_flags(def);
  out->excludeMask      = decal_build_mask(&def->excludeMask);
  out->roughness        = def->roughness;
  out->alphaMin         = def->alphaMin < f32_epsilon ? 1.0f : def->alphaMin;
  out->alphaMax         = math_max(out->alphaMin, def->alphaMax);
  out->width            = def->width > f32_epsilon ? def->width : decal_default_size;
  out->height           = def->height > f32_epsilon ? def->height : decal_default_size;
  out->thickness        = def->thickness > f32_epsilon ? def->thickness : decal_default_thickness;
  out->scaleMin         = def->scaleMin < f32_epsilon ? 1.0f : def->scaleMin;
  out->scaleMax         = math_max(out->scaleMin, def->scaleMax);
  out->fadeInTime       = (TimeDuration)time_seconds(def->fadeInTime);
  out->fadeOutTime      = (TimeDuration)time_seconds(def->fadeOutTime);
}

ecs_module_init(asset_decal_module) {
  decal_datareg_init();

  ecs_register_comp(AssetDecalComp);

  ecs_register_view(DecalUnloadView);

  ecs_register_system(DecalUnloadAssetSys, ecs_view_id(DecalUnloadView));
}

void asset_load_decal(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;

  DecalDef       decalDef;
  String         errMsg;
  DataReadResult readRes;
  data_read_json(
      g_dataReg, src->data, g_allocHeap, g_dataDecalDefMeta, mem_var(decalDef), &readRes);
  if (UNLIKELY(readRes.error)) {
    errMsg = readRes.errorMsg;
    goto Error;
  }

  if (UNLIKELY(decalDef.trail && decalDef.projectionAxis != AssetDecalAxis_WorldY)) {
    errMsg = string_lit("Trail decals only support 'WorldY' projection");
    goto Error;
  }

  AssetDecalComp* result = ecs_world_add_t(world, entity, AssetDecalComp);
  decal_build_def(&decalDef, result);

  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  goto Cleanup;

Error:
  log_e("Failed to load Decal", log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);

Cleanup:
  data_destroy(g_dataReg, g_allocHeap, g_dataDecalDefMeta, mem_var(decalDef));
  asset_repo_source_close(src);
}

void asset_decal_jsonschema_write(DynString* str) {
  decal_datareg_init();

  const DataJsonSchemaFlags schemaFlags = DataJsonSchemaFlags_Compact;
  data_jsonschema_write(g_dataReg, str, g_dataDecalDefMeta, schemaFlags);
}
