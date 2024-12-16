#include "asset_vfx.h"
#include "core_alloc.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "core_time.h"
#include "data_read.h"
#include "data_utils.h"
#include "ecs_entity.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "data_internal.h"
#include "manager_internal.h"
#include "repo_internal.h"

#define vfx_max_time time_days(9999)

DataMeta g_assetVfxDefMeta;

typedef struct {
  f32       angle;
  f32       radius;
  GeoVector position;
  GeoVector rotation;
} AssetVfxConeDef;

typedef struct {
  GeoVector base, random;
} AssetVfxRangeRotationDef;

typedef struct {
  StringHash     atlasEntry;
  GeoColor*      color;
  AssetVfxBlend  blend;
  AssetVfxFacing facing;
  u16            flipbookCount;
  f32            flipbookTime;
  GeoVector      size;
  f32            fadeInTime, fadeOutTime;
  f32            scaleInTime, scaleOutTime;
  bool           geometryFade, shadowCaster, distortion;
} AssetVfxSpriteDef;

typedef struct {
  GeoColor radiance;
  f32      fadeInTime, fadeOutTime;
  f32      radius;
  f32      turbulenceFrequency;
} AssetVfxLightDef;

typedef struct {
  AssetVfxConeDef          cone;
  GeoVector                force;
  f32                      friction;
  AssetVfxSpace            space;
  AssetVfxSpriteDef        sprite;
  AssetVfxLightDef         light;
  AssetVfxRangeScalar      speed;
  f32                      expandForce;
  u16                      count;
  TimeDuration             interval;
  AssetVfxRangeScalar      scale;
  AssetVfxRangeDuration    lifetime;
  AssetVfxRangeRotationDef rotation;
} AssetVfxEmitterDef;

typedef struct {
  bool ignoreTransformRotation;
  HeapArray_t(AssetVfxEmitterDef) emitters;
} AssetVfxDef;

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

static void ecs_destruct_vfx_comp(void* data) {
  AssetVfxComp* comp = data;
  alloc_free_array_t(g_allocHeap, comp->emitters.values, comp->emitters.count);
}

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

static AssetVfxCone vfx_build_cone(const AssetVfxConeDef* def) {
  return (AssetVfxCone){
      .angle    = def->angle,
      .radius   = def->radius,
      .position = def->position,
      .rotation = geo_quat_from_euler(geo_vector_mul(def->rotation, math_deg_to_rad)),
  };
}

static AssetVfxRangeRotation vfx_build_range_rotation(const AssetVfxRangeRotationDef* def) {
  return (AssetVfxRangeRotation){
      .base              = geo_quat_from_euler(geo_vector_mul(def->base, math_deg_to_rad)),
      .randomEulerAngles = geo_vector_mul(def->random, math_deg_to_rad),
  };
}

static void vfx_build_sprite(const AssetVfxSpriteDef* def, AssetVfxSprite* out) {
  if (!def->atlasEntry) {
    *out = (AssetVfxSprite){0};
    return; // Sprites are optional.
  }
  out->atlasEntry      = def->atlasEntry;
  out->color           = def->color ? *def->color : geo_color_white;
  out->blend           = def->blend;
  out->facing          = def->facing;
  out->flipbookCount   = math_max(1, def->flipbookCount);
  out->flipbookTimeInv = 1.0f / math_max(def->flipbookTime, 0.01f);
  out->sizeX           = def->size.x;
  out->sizeY           = def->size.y;
  out->fadeInTimeInv   = (def->fadeInTime > f32_epsilon) ? (1.0f / def->fadeInTime) : f32_max;
  out->fadeOutTimeInv  = (def->fadeOutTime > f32_epsilon) ? (1.0f / def->fadeOutTime) : f32_max;
  out->scaleInTimeInv  = (def->scaleInTime > f32_epsilon) ? (1.0f / def->scaleInTime) : f32_max;
  out->scaleOutTimeInv = (def->scaleOutTime > f32_epsilon) ? (1.0f / def->scaleOutTime) : f32_max;
  out->geometryFade    = def->geometryFade;
  out->shadowCaster    = def->shadowCaster;
  out->distortion      = def->distortion;
}

static void vfx_build_light(const AssetVfxLightDef* def, AssetVfxLight* out) {
  if (def->radiance.a <= f32_epsilon) {
    *out = (AssetVfxLight){0};
    return; // Lights are optional.
  }
  out->radiance            = def->radiance;
  out->fadeInTimeInv       = (def->fadeInTime > f32_epsilon) ? (1.0f / def->fadeInTime) : f32_max;
  out->fadeOutTimeInv      = (def->fadeOutTime > f32_epsilon) ? (1.0f / def->fadeOutTime) : f32_max;
  out->radius              = def->radius > f32_epsilon ? def->radius : 10.0f;
  out->turbulenceFrequency = def->turbulenceFrequency;
}

static void vfx_build_emitter(const AssetVfxEmitterDef* def, AssetVfxEmitter* out) {
  out->cone     = vfx_build_cone(&def->cone);
  out->force    = def->force;
  out->friction = def->friction > f32_epsilon ? def->friction : 1.0f;
  out->space    = def->space;

  vfx_build_sprite(&def->sprite, &out->sprite);
  vfx_build_light(&def->light, &out->light);

  out->speed       = def->speed;
  out->expandForce = def->expandForce;
  out->count       = def->count;
  out->interval    = def->interval;

  out->scale = def->scale;
  if (out->scale.max <= 0) {
    out->scale.min = out->scale.max = 1.0f;
  }

  out->lifetime = def->lifetime;
  if (out->lifetime.max <= 0) {
    out->lifetime.min = out->lifetime.max = vfx_max_time;
  }

  out->rotation = vfx_build_range_rotation(&def->rotation);
}

static void vfx_build_def(const AssetVfxDef* def, AssetVfxComp* out) {
  diag_assert(def->emitters.count <= asset_vfx_max_emitters);

  AssetVfxFlags flags = 0;
  if (def->ignoreTransformRotation) {
    flags |= AssetVfx_IgnoreTransformRotation;
  }
  out->flags           = flags;
  out->emitters.values = alloc_array_t(g_allocHeap, AssetVfxEmitter, def->emitters.count);
  out->emitters.count  = (u32)def->emitters.count;

  for (u32 i = 0; i != out->emitters.count; ++i) {
    vfx_build_emitter(&def->emitters.values[i], &out->emitters.values[i]);
  }
}

ecs_module_init(asset_vfx_module) {
  ecs_register_comp(AssetVfxComp, .destructor = ecs_destruct_vfx_comp);

  ecs_register_view(VfxUnloadView);

  ecs_register_system(VfxUnloadAssetSys, ecs_view_id(VfxUnloadView));
}

static bool vfx_data_normalizer_range_scalar(const Mem data) {
  AssetVfxRangeScalar* range = mem_as_t(data, AssetVfxRangeScalar);
  range->max                 = math_max(range->min, range->max);
  return true;
}

static bool vfx_data_normalizer_range_duration(const Mem data) {
  AssetVfxRangeDuration* range = mem_as_t(data, AssetVfxRangeDuration);
  range->max                   = math_max(range->min, range->max);
  return true;
}

void asset_data_init_vfx(void) {
  // clang-format off
  data_reg_struct_t(g_dataReg, AssetVfxConeDef);
  data_reg_field_t(g_dataReg, AssetVfxConeDef, angle, data_prim_t(Angle), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxConeDef, radius, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxConeDef, position, g_assetGeoVec3Type, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxConeDef, rotation, g_assetGeoVec3Type, .flags = DataFlags_Opt);
  data_reg_comment_t(g_dataReg, AssetVfxConeDef, "3D Cone shape");

  data_reg_struct_t(g_dataReg, AssetVfxRangeScalar);
  data_reg_field_t(g_dataReg, AssetVfxRangeScalar, min, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxRangeScalar, max, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_normalizer_t(g_dataReg, AssetVfxRangeScalar, vfx_data_normalizer_range_scalar);

  data_reg_struct_t(g_dataReg, AssetVfxRangeDuration);
  data_reg_field_t(g_dataReg, AssetVfxRangeDuration, min, data_prim_t(TimeDuration), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxRangeDuration, max, data_prim_t(TimeDuration), .flags = DataFlags_Opt);
  data_reg_normalizer_t(g_dataReg, AssetVfxRangeDuration, vfx_data_normalizer_range_duration);

  data_reg_struct_t(g_dataReg, AssetVfxRangeRotationDef);
  data_reg_field_t(g_dataReg, AssetVfxRangeRotationDef, base, g_assetGeoVec3Type, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxRangeRotationDef, random, g_assetGeoVec3Type, .flags = DataFlags_Opt);

  data_reg_enum_t(g_dataReg, AssetVfxSpace);
  data_reg_const_t(g_dataReg, AssetVfxSpace, Local);
  data_reg_const_t(g_dataReg, AssetVfxSpace, World);
  data_reg_comment_t(g_dataReg, AssetVfxSpace, "* Local: Particles are simulated relative to the entity transform.\n"
                                          "* World: Particles are simulated in world-space.");

  data_reg_enum_t(g_dataReg, AssetVfxBlend);
  data_reg_const_t(g_dataReg, AssetVfxBlend, None);
  data_reg_const_t(g_dataReg, AssetVfxBlend, Alpha);
  data_reg_const_t(g_dataReg, AssetVfxBlend, Additive);
  data_reg_comment_t(g_dataReg, AssetVfxBlend, "* None: Sprites are not blended.\n"
                                          "* Alpha: Sprites are interpolated based on the alpha.\n"
                                          "* World: Sprites are additively blended.");

  data_reg_enum_t(g_dataReg, AssetVfxFacing);
  data_reg_const_t(g_dataReg, AssetVfxFacing, Local);
  data_reg_const_t(g_dataReg, AssetVfxFacing, BillboardSphere);
  data_reg_const_t(g_dataReg, AssetVfxFacing, BillboardCylinder);
  data_reg_comment_t(g_dataReg, AssetVfxFacing, "* Local: Sprites are facing in the particle orientation.\n"
                                          "* BillboardSphere: Sprites are camera facing.\n"
                                          "* BillboardCylinder: Sprites are camera facing on the Y axis.");


  data_reg_struct_t(g_dataReg, AssetVfxSpriteDef);
  data_reg_field_t(g_dataReg, AssetVfxSpriteDef, atlasEntry, data_prim_t(StringHash), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetVfxSpriteDef, color, g_assetGeoColor4Type, .container = DataContainer_Pointer, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxSpriteDef, blend, t_AssetVfxBlend, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxSpriteDef, facing, t_AssetVfxFacing, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxSpriteDef, flipbookCount, data_prim_t(u16), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxSpriteDef, flipbookTime, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxSpriteDef, size, g_assetGeoVec2Type);
  data_reg_field_t(g_dataReg, AssetVfxSpriteDef, fadeInTime, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxSpriteDef, fadeOutTime, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxSpriteDef, scaleInTime, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxSpriteDef, scaleOutTime, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxSpriteDef, geometryFade, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxSpriteDef, shadowCaster, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxSpriteDef, distortion, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_comment_t(g_dataReg, AssetVfxSpriteDef, "Optional sprite to render for each particle.");

  data_reg_struct_t(g_dataReg, AssetVfxLightDef);
  data_reg_field_t(g_dataReg, AssetVfxLightDef, radiance, g_assetGeoColor4Type, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxLightDef, fadeInTime, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxLightDef, fadeOutTime, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxLightDef, radius, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxLightDef, turbulenceFrequency, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_comment_t(g_dataReg, AssetVfxLightDef, "Optional point light to render for each particle.");

  data_reg_struct_t(g_dataReg, AssetVfxEmitterDef);
  data_reg_field_t(g_dataReg, AssetVfxEmitterDef, cone, t_AssetVfxConeDef, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxEmitterDef, force, g_assetGeoVec3Type, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxEmitterDef, friction, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetVfxEmitterDef, space, t_AssetVfxSpace, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxEmitterDef, sprite, t_AssetVfxSpriteDef, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxEmitterDef, light, t_AssetVfxLightDef, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxEmitterDef, speed, t_AssetVfxRangeScalar, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxEmitterDef, expandForce, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxEmitterDef, count, data_prim_t(u16), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxEmitterDef, interval, data_prim_t(TimeDuration), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxEmitterDef, scale, t_AssetVfxRangeScalar, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxEmitterDef, lifetime, t_AssetVfxRangeDuration, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxEmitterDef, rotation, t_AssetVfxRangeRotationDef, .flags = DataFlags_Opt);
  data_reg_comment_t(g_dataReg, AssetVfxEmitterDef, "Particle emitter settings.");

  data_reg_struct_t(g_dataReg, AssetVfxDef);
  data_reg_field_t(g_dataReg, AssetVfxDef, ignoreTransformRotation, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetVfxDef, emitters, t_AssetVfxEmitterDef, .container = DataContainer_HeapArray);
  // clang-format on

  g_assetVfxDefMeta = data_meta_t(t_AssetVfxDef);
}

void asset_load_vfx(
    EcsWorld*                 world,
    const AssetImportEnvComp* importEnv,
    const String              id,
    const EcsEntityId         entity,
    AssetSource*              src) {
  (void)importEnv;

  AssetVfxDef    vfxDef;
  String         errMsg;
  DataReadResult readRes;
  data_read_json(g_dataReg, src->data, g_allocHeap, g_assetVfxDefMeta, mem_var(vfxDef), &readRes);
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
  log_e(
      "Failed to load Vfx",
      log_param("id", fmt_text(id)),
      log_param("entity", ecs_entity_fmt(entity)),
      log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);

Cleanup:
  data_destroy(g_dataReg, g_allocHeap, g_assetVfxDefMeta, mem_var(vfxDef));
  asset_repo_source_close(src);
}
