#include "asset_vfx.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "core_thread.h"
#include "core_time.h"
#include "data.h"
#include "data_schema.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "manager_internal.h"
#include "repo_internal.h"

#define vfx_max_time time_days(9999)

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
  VfxColorDef*   color;
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
  VfxColorDef radiance;
  f32         fadeInTime, fadeOutTime;
  f32         radius;
  f32         turbulenceFrequency;
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
  u32                 count;
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

static void vfx_datareg_init(void) {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    DataReg* reg = data_reg_create(g_allocPersist);

    // clang-format off
    data_reg_struct_t(reg, VfxVec2Def);
    data_reg_field_t(reg, VfxVec2Def, x, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxVec2Def, y, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_comment_t(reg, VfxVec2Def, "2D Vector (components default to 0)");

    data_reg_struct_t(reg, VfxVec3Def);
    data_reg_field_t(reg, VfxVec3Def, x, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxVec3Def, y, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxVec3Def, z, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_comment_t(reg, VfxVec3Def, "3D Vector (components default to 0)");

    data_reg_struct_t(reg, VfxRotDef);
    data_reg_field_t(reg, VfxRotDef, x, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxRotDef, y, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxRotDef, z, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_comment_t(reg, VfxRotDef, "3D Rotation (components default to 0)");

    data_reg_struct_t(reg, VfxColorDef);
    data_reg_field_t(reg, VfxColorDef, r, data_prim_t(f32));
    data_reg_field_t(reg, VfxColorDef, g, data_prim_t(f32));
    data_reg_field_t(reg, VfxColorDef, b, data_prim_t(f32));
    data_reg_field_t(reg, VfxColorDef, a, data_prim_t(f32));
    data_reg_comment_t(reg, VfxColorDef, "HDR Color definition (components default to 0)");

    data_reg_struct_t(reg, VfxConeDef);
    data_reg_field_t(reg, VfxConeDef, angle, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxConeDef, radius, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxConeDef, position, t_VfxVec3Def, .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxConeDef, rotation, t_VfxRotDef, .flags = DataFlags_Opt);
    data_reg_comment_t(reg, VfxConeDef, "3D Cone shape");

    data_reg_struct_t(reg, VfxRangeScalarDef);
    data_reg_field_t(reg, VfxRangeScalarDef, min, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxRangeScalarDef, max, data_prim_t(f32), .flags = DataFlags_Opt);

    data_reg_struct_t(reg, VfxRangeDurationDef);
    data_reg_field_t(reg, VfxRangeDurationDef, min, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxRangeDurationDef, max, data_prim_t(f32), .flags = DataFlags_Opt);

    data_reg_struct_t(reg, VfxRangeRotationDef);
    data_reg_field_t(reg, VfxRangeRotationDef, base, t_VfxRotDef, .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxRangeRotationDef, random, t_VfxRotDef, .flags = DataFlags_Opt);

    data_reg_enum_t(reg, AssetVfxSpace);
    data_reg_const_t(reg, AssetVfxSpace, Local);
    data_reg_const_t(reg, AssetVfxSpace, World);
    data_reg_comment_t(reg, AssetVfxSpace, "* Local: Particles are simulated relative to the entity transform.\n"
                                           "* World: Particles are simulated in world-space.");

    data_reg_enum_t(reg, AssetVfxBlend);
    data_reg_const_t(reg, AssetVfxBlend, None);
    data_reg_const_t(reg, AssetVfxBlend, Alpha);
    data_reg_const_t(reg, AssetVfxBlend, Additive);
    data_reg_comment_t(reg, AssetVfxBlend, "* None: Sprites are not blended.\n"
                                           "* Alpha: Sprites are interpolated based on the alpha.\n"
                                           "* World: Sprites are additively blended.");

    data_reg_enum_t(reg, AssetVfxFacing);
    data_reg_const_t(reg, AssetVfxFacing, Local);
    data_reg_const_t(reg, AssetVfxFacing, BillboardSphere);
    data_reg_const_t(reg, AssetVfxFacing, BillboardCylinder);
    data_reg_comment_t(reg, AssetVfxFacing, "* Local: Sprites are facing in the particle orientation.\n"
                                            "* BillboardSphere: Sprites are camera facing.\n"
                                            "* BillboardCylinder: Sprites are camera facing on the Y axis.");


    data_reg_struct_t(reg, VfxSpriteDef);
    data_reg_field_t(reg, VfxSpriteDef, atlasEntry, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, VfxSpriteDef, color, t_VfxColorDef, .container = DataContainer_Pointer, .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxSpriteDef, blend, t_AssetVfxBlend, .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxSpriteDef, facing, t_AssetVfxFacing, .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxSpriteDef, flipbookCount, data_prim_t(u16), .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxSpriteDef, flipbookTime, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxSpriteDef, size, t_VfxVec2Def);
    data_reg_field_t(reg, VfxSpriteDef, fadeInTime, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxSpriteDef, fadeOutTime, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxSpriteDef, scaleInTime, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxSpriteDef, scaleOutTime, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxSpriteDef, geometryFade, data_prim_t(bool), .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxSpriteDef, shadowCaster, data_prim_t(bool), .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxSpriteDef, distortion, data_prim_t(bool), .flags = DataFlags_Opt);
    data_reg_comment_t(reg, VfxSpriteDef, "Optional sprite to render for each particle.");

    data_reg_struct_t(reg, VfxLightDef);
    data_reg_field_t(reg, VfxLightDef, radiance, t_VfxColorDef, .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxLightDef, fadeInTime, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxLightDef, fadeOutTime, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxLightDef, radius, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxLightDef, turbulenceFrequency, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_comment_t(reg, VfxLightDef, "Optional point light to render for each particle.");

    data_reg_struct_t(reg, VfxEmitterDef);
    data_reg_field_t(reg, VfxEmitterDef, cone, t_VfxConeDef, .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxEmitterDef, force, t_VfxVec3Def, .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxEmitterDef, friction, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, VfxEmitterDef, space, t_AssetVfxSpace, .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxEmitterDef, sprite, t_VfxSpriteDef, .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxEmitterDef, light, t_VfxLightDef, .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxEmitterDef, speed, t_VfxRangeScalarDef, .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxEmitterDef, expandForce, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxEmitterDef, count, data_prim_t(u32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxEmitterDef, interval, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxEmitterDef, scale, t_VfxRangeScalarDef, .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxEmitterDef, lifetime, t_VfxRangeDurationDef, .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxEmitterDef, rotation, t_VfxRangeRotationDef, .flags = DataFlags_Opt);
    data_reg_comment_t(reg, VfxEmitterDef, "Particle emitter settings.");

    data_reg_struct_t(reg, VfxDef);
    data_reg_field_t(reg, VfxDef, ignoreTransformRotation, data_prim_t(bool), .flags = DataFlags_Opt);
    data_reg_field_t(reg, VfxDef, emitters, t_VfxEmitterDef, .container = DataContainer_Array);
    // clang-format on

    g_dataVfxDefMeta = data_meta_t(t_VfxDef);
    g_dataReg        = reg;
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

static GeoColor vfx_build_color(const VfxColorDef* def) {
  return geo_color(def->r, def->g, def->b, def->a);
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
  out->atlasEntry    = string_hash(def->atlasEntry);
  out->color         = def->color ? vfx_build_color(def->color) : geo_color_white;
  out->blend         = def->blend;
  out->facing        = def->facing;
  out->flipbookCount = math_max(1, def->flipbookCount);
  out->flipbookTime  = math_max(time_millisecond, (TimeDuration)time_seconds(def->flipbookTime));
  out->sizeX         = def->size.x;
  out->sizeY         = def->size.y;
  out->fadeInTime    = (TimeDuration)time_seconds(def->fadeInTime);
  out->fadeOutTime   = (TimeDuration)time_seconds(def->fadeOutTime);
  out->scaleInTime   = (TimeDuration)time_seconds(def->scaleInTime);
  out->scaleOutTime  = (TimeDuration)time_seconds(def->scaleOutTime);
  out->geometryFade  = def->geometryFade;
  out->shadowCaster  = def->shadowCaster;
  out->distortion    = def->distortion;
}

static void vfx_build_light(const VfxLightDef* def, AssetVfxLight* out) {
  if (def->radiance.a <= f32_epsilon) {
    *out = (AssetVfxLight){0};
    return; // Lights are optional.
  }
  out->radiance            = vfx_build_color(&def->radiance);
  out->fadeInTime          = (TimeDuration)time_seconds(def->fadeInTime);
  out->fadeOutTime         = (TimeDuration)time_seconds(def->fadeOutTime);
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
  vfx_datareg_init();

  ecs_register_comp(AssetVfxComp, .destructor = ecs_destruct_vfx_comp);

  ecs_register_view(VfxUnloadView);

  ecs_register_system(VfxUnloadAssetSys, ecs_view_id(VfxUnloadView));
}

void asset_load_vfx(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;

  VfxDef         vfxDef;
  String         errMsg;
  DataReadResult readRes;
  data_read_json(g_dataReg, src->data, g_allocHeap, g_dataVfxDefMeta, mem_var(vfxDef), &readRes);
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
  data_destroy(g_dataReg, g_allocHeap, g_dataVfxDefMeta, mem_var(vfxDef));
  asset_repo_source_close(src);
}

void asset_vfx_jsonschema_write(DynString* str) {
  vfx_datareg_init();

  const DataJsonSchemaFlags schemaFlags = DataJsonSchemaFlags_Compact;
  data_jsonschema_write(g_dataReg, str, g_dataVfxDefMeta, schemaFlags);
}
