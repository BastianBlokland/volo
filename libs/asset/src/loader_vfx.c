#include "asset_vfx.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "core_time.h"
#include "data.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "data_internal.h"
#include "manager_internal.h"
#include "repo_internal.h"

#define vfx_max_time time_days(9999)

DataMeta g_assetVfxDefMeta;

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
  f32        angle;
  f32        radius;
  VfxVec3Def position;
  VfxRotDef  rotation;
} VfxConeDef;

typedef struct {
  f32 min, max;
} VfxRangeScalarDef;

typedef struct {
  f32 min, max;
} VfxRangeDurationDef;

typedef struct {
  VfxRotDef base, random;
} VfxRangeRotationDef;

typedef struct {
  String         atlasEntry;
  GeoColor*      color;
  AssetVfxBlend  blend;
  AssetVfxFacing facing;
  u16            flipbookCount;
  f32            flipbookTime;
  VfxVec2Def     size;
  f32            fadeInTime, fadeOutTime;
  f32            scaleInTime, scaleOutTime;
  bool           geometryFade, shadowCaster, distortion;
} VfxSpriteDef;

typedef struct {
  GeoColor radiance;
  f32      fadeInTime, fadeOutTime;
  f32      radius;
  f32      turbulenceFrequency;
} VfxLightDef;

typedef struct {
  VfxConeDef          cone;
  VfxVec3Def          force;
  f32                 friction;
  AssetVfxSpace       space;
  VfxSpriteDef        sprite;
  VfxLightDef         light;
  VfxRangeScalarDef   speed;
  f32                 expandForce;
  u16                 count;
  f32                 interval;
  VfxRangeScalarDef   scale;
  VfxRangeDurationDef lifetime;
  VfxRangeRotationDef rotation;
} VfxEmitterDef;

typedef struct {
  bool ignoreTransformRotation;
  struct {
    VfxEmitterDef* values;
    usize          count;
  } emitters;
} VfxDef;

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
  alloc_free_array_t(g_allocHeap, comp->emitters, comp->emitterCount);
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

static GeoVector vfx_build_vec3(const VfxVec3Def* def) {
  return geo_vector(def->x, def->y, def->z);
}

static GeoQuat vfx_build_rot(const VfxRotDef* def) {
  const GeoVector eulerAnglesDeg = geo_vector(def->x, def->y, def->z);
  return geo_quat_from_euler(geo_vector_mul(eulerAnglesDeg, math_deg_to_rad));
}

static AssetVfxCone vfx_build_cone(const VfxConeDef* def) {
  return (AssetVfxCone){
      .angle    = def->angle * math_deg_to_rad,
      .radius   = def->radius,
      .position = vfx_build_vec3(&def->position),
      .rotation = vfx_build_rot(&def->rotation),
  };
}

static AssetVfxRangeScalar vfx_build_range_scalar(const VfxRangeScalarDef* def) {
  return (AssetVfxRangeScalar){
      .min = def->min,
      .max = math_max(def->min, def->max),
  };
}

static AssetVfxRangeDuration vfx_build_range_duration(const VfxRangeDurationDef* def) {
  return (AssetVfxRangeDuration){
      .min = (TimeDuration)time_seconds(def->min),
      .max = (TimeDuration)time_seconds(math_max(def->min, def->max)),
  };
}

static AssetVfxRangeRotation vfx_build_range_rotation(const VfxRangeRotationDef* def) {
  const GeoVector randomEulerAnglesDeg = geo_vector(def->random.x, def->random.y, def->random.z);
  return (AssetVfxRangeRotation){
      .base              = vfx_build_rot(&def->base),
      .randomEulerAngles = geo_vector_mul(randomEulerAnglesDeg, math_deg_to_rad),
  };
}

static void vfx_build_sprite(const VfxSpriteDef* def, AssetVfxSprite* out) {
  if (string_is_empty(def->atlasEntry)) {
    *out = (AssetVfxSprite){0};
    return; // Sprites are optional.
  }
  out->atlasEntry      = string_hash(def->atlasEntry);
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

static void vfx_build_light(const VfxLightDef* def, AssetVfxLight* out) {
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

static void vfx_build_emitter(const VfxEmitterDef* def, AssetVfxEmitter* out) {
  out->cone     = vfx_build_cone(&def->cone);
  out->force    = vfx_build_vec3(&def->force);
  out->friction = def->friction > f32_epsilon ? def->friction : 1.0f;
  out->space    = def->space;

  vfx_build_sprite(&def->sprite, &out->sprite);
  vfx_build_light(&def->light, &out->light);

  out->speed       = vfx_build_range_scalar(&def->speed);
  out->expandForce = def->expandForce;
  out->count       = def->count;
  out->interval    = (TimeDuration)time_seconds(def->interval);

  out->scale = vfx_build_range_scalar(&def->scale);
  if (out->scale.max <= 0) {
    out->scale.min = out->scale.max = 1.0f;
  }

  out->lifetime = vfx_build_range_duration(&def->lifetime);
  if (out->lifetime.max <= 0) {
    out->lifetime.min = out->lifetime.max = vfx_max_time;
  }

  out->rotation = vfx_build_range_rotation(&def->rotation);
}

static void vfx_build_def(const VfxDef* def, AssetVfxComp* out) {
  diag_assert(def->emitters.count <= asset_vfx_max_emitters);

  AssetVfxFlags flags = 0;
  if (def->ignoreTransformRotation) {
    flags |= AssetVfx_IgnoreTransformRotation;
  }
  out->flags        = flags;
  out->emitters     = alloc_array_t(g_allocHeap, AssetVfxEmitter, def->emitters.count);
  out->emitterCount = (u32)def->emitters.count;

  for (u32 i = 0; i != out->emitterCount; ++i) {
    vfx_build_emitter(&def->emitters.values[i], &out->emitters[i]);
  }
}

ecs_module_init(asset_vfx_module) {
  ecs_register_comp(AssetVfxComp, .destructor = ecs_destruct_vfx_comp);

  ecs_register_view(VfxUnloadView);

  ecs_register_system(VfxUnloadAssetSys, ecs_view_id(VfxUnloadView));
}

void asset_data_init_vfx(void) {
  // clang-format off
  data_reg_struct_t(g_dataReg, VfxVec2Def);
  data_reg_field_t(g_dataReg, VfxVec2Def, x, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxVec2Def, y, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_comment_t(g_dataReg, VfxVec2Def, "2D Vector (components default to 0)");

  data_reg_struct_t(g_dataReg, VfxVec3Def);
  data_reg_field_t(g_dataReg, VfxVec3Def, x, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxVec3Def, y, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxVec3Def, z, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_comment_t(g_dataReg, VfxVec3Def, "3D Vector (components default to 0)");

  data_reg_struct_t(g_dataReg, VfxRotDef);
  data_reg_field_t(g_dataReg, VfxRotDef, x, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxRotDef, y, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxRotDef, z, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_comment_t(g_dataReg, VfxRotDef, "3D Rotation (components default to 0)");

  data_reg_struct_t(g_dataReg, VfxConeDef);
  data_reg_field_t(g_dataReg, VfxConeDef, angle, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxConeDef, radius, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxConeDef, position, t_VfxVec3Def, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxConeDef, rotation, t_VfxRotDef, .flags = DataFlags_Opt);
  data_reg_comment_t(g_dataReg, VfxConeDef, "3D Cone shape");

  data_reg_struct_t(g_dataReg, VfxRangeScalarDef);
  data_reg_field_t(g_dataReg, VfxRangeScalarDef, min, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxRangeScalarDef, max, data_prim_t(f32), .flags = DataFlags_Opt);

  data_reg_struct_t(g_dataReg, VfxRangeDurationDef);
  data_reg_field_t(g_dataReg, VfxRangeDurationDef, min, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxRangeDurationDef, max, data_prim_t(f32), .flags = DataFlags_Opt);

  data_reg_struct_t(g_dataReg, VfxRangeRotationDef);
  data_reg_field_t(g_dataReg, VfxRangeRotationDef, base, t_VfxRotDef, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxRangeRotationDef, random, t_VfxRotDef, .flags = DataFlags_Opt);

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


  data_reg_struct_t(g_dataReg, VfxSpriteDef);
  data_reg_field_t(g_dataReg, VfxSpriteDef, atlasEntry, data_prim_t(String), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, VfxSpriteDef, color, g_assetGeoColorType, .container = DataContainer_Pointer, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxSpriteDef, blend, t_AssetVfxBlend, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxSpriteDef, facing, t_AssetVfxFacing, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxSpriteDef, flipbookCount, data_prim_t(u16), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxSpriteDef, flipbookTime, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxSpriteDef, size, t_VfxVec2Def);
  data_reg_field_t(g_dataReg, VfxSpriteDef, fadeInTime, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxSpriteDef, fadeOutTime, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxSpriteDef, scaleInTime, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxSpriteDef, scaleOutTime, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxSpriteDef, geometryFade, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxSpriteDef, shadowCaster, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxSpriteDef, distortion, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_comment_t(g_dataReg, VfxSpriteDef, "Optional sprite to render for each particle.");

  data_reg_struct_t(g_dataReg, VfxLightDef);
  data_reg_field_t(g_dataReg, VfxLightDef, radiance, g_assetGeoColorType, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxLightDef, fadeInTime, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxLightDef, fadeOutTime, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxLightDef, radius, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxLightDef, turbulenceFrequency, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_comment_t(g_dataReg, VfxLightDef, "Optional point light to render for each particle.");

  data_reg_struct_t(g_dataReg, VfxEmitterDef);
  data_reg_field_t(g_dataReg, VfxEmitterDef, cone, t_VfxConeDef, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxEmitterDef, force, t_VfxVec3Def, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxEmitterDef, friction, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, VfxEmitterDef, space, t_AssetVfxSpace, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxEmitterDef, sprite, t_VfxSpriteDef, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxEmitterDef, light, t_VfxLightDef, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxEmitterDef, speed, t_VfxRangeScalarDef, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxEmitterDef, expandForce, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxEmitterDef, count, data_prim_t(u16), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxEmitterDef, interval, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxEmitterDef, scale, t_VfxRangeScalarDef, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxEmitterDef, lifetime, t_VfxRangeDurationDef, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxEmitterDef, rotation, t_VfxRangeRotationDef, .flags = DataFlags_Opt);
  data_reg_comment_t(g_dataReg, VfxEmitterDef, "Particle emitter settings.");

  data_reg_struct_t(g_dataReg, VfxDef);
  data_reg_field_t(g_dataReg, VfxDef, ignoreTransformRotation, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, VfxDef, emitters, t_VfxEmitterDef, .container = DataContainer_DataArray);
  // clang-format on

  g_assetVfxDefMeta = data_meta_t(t_VfxDef);
}

void asset_load_vfx(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  VfxDef         vfxDef;
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
  log_e("Failed to load Vfx", log_param("id", fmt_text(id)), log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);

Cleanup:
  data_destroy(g_dataReg, g_allocHeap, g_assetVfxDefMeta, mem_var(vfxDef));
  asset_repo_source_close(src);
}
