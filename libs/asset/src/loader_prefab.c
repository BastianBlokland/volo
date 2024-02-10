#include "asset_prefab.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "core_search.h"
#include "core_stringtable.h"
#include "core_thread.h"
#include "data.h"
#include "data_schema.h"
#include "ecs_utils.h"
#include "log_logger.h"

#include "manager_internal.h"
#include "repo_internal.h"

#define trait_movement_weight_min 0.1f

static DataReg* g_dataReg;
static DataMeta g_dataMapDefMeta;

static struct {
  String           setName;
  StringHash       set;
  AssetPrefabFlags flags;
} g_prefabSetFlags[] = {
    {.setName = string_static("infantry"), .flags = AssetPrefabFlags_Infantry},
    {.setName = string_static("vehicle"), .flags = AssetPrefabFlags_Vehicle},
    {.setName = string_static("structure"), .flags = AssetPrefabFlags_Structure},
    {.setName = string_static("destructible"), .flags = AssetPrefabFlags_Destructible},
};

static void prefab_set_flags_init() {
  for (u32 i = 0; i != array_elems(g_prefabSetFlags); ++i) {
    g_prefabSetFlags[i].set = string_hash(g_prefabSetFlags[i].setName);
  }
}

static AssetPrefabFlags prefab_set_flags(const StringHash set) {
  for (u32 i = 0; i != array_elems(g_prefabSetFlags); ++i) {
    if (g_prefabSetFlags[i].set == set) {
      return g_prefabSetFlags[i].flags;
    }
  }
  return 0;
}

typedef struct {
  f32 x, y, z;
} AssetPrefabVec3Def;

typedef struct {
  f32 r, g, b, a;
} AssetPrefabColorDef;

typedef struct {
  AssetPrefabVec3Def offset;
  f32                radius;
} AssetPrefabShapeSphereDef;

typedef struct {
  AssetPrefabVec3Def offset;
  f32                radius, height;
} AssetPrefabShapeCapsuleDef;

typedef struct {
  AssetPrefabVec3Def min, max;
} AssetPrefabShapeBoxDef;

typedef struct {
  AssetPrefabShapeType type;
  union {
    AssetPrefabShapeSphereDef  data_sphere;
    AssetPrefabShapeCapsuleDef data_capsule;
    AssetPrefabShapeBoxDef     data_box;
  };
} AssetPrefabShapeDef;

typedef struct {
  String assetId;
  bool   persistent;
} AssetPrefabValueSoundDef;

typedef struct {
  String               name;
  AssetPrefabValueType type;
  union {
    f64                      data_number;
    bool                     data_bool;
    AssetPrefabVec3Def       data_vector3;
    AssetPrefabColorDef      data_color;
    String                   data_string;
    String                   data_asset;
    AssetPrefabValueSoundDef data_sound;
  };
} AssetPrefabValueDef;

typedef struct {
  String name;
} AssetPrefabTraitNameDef;

typedef struct {
  struct {
    String* values;
    usize   count;
  } sets;
} AssetPrefabTraitSetMemberDef;

typedef struct {
  String graphicId;
} AssetPrefabTraitRenderableDef;

typedef struct {
  String assetId;
} AssetPrefabTraitVfxDef;

typedef struct {
  String assetId;
} AssetPrefabTraitDecalDef;

typedef struct {
  struct {
    String* values;
    usize   count;
  } assetIds;
  f32  gainMin, gainMax;
  f32  pitchMin, pitchMax;
  bool looping;
  bool persistent;
} AssetPrefabTraitSoundDef;

typedef struct {
  AssetPrefabColorDef radiance;
  f32                 radius;
} AssetPrefabTraitLightPointDef;

typedef struct {
  AssetPrefabColorDef radiance;
  bool                shadows, coverage;
} AssetPrefabTraitLightDirDef;

typedef struct {
  f32 intensity;
} AssetPrefabTraitLightAmbientDef;

typedef struct {
  f32 duration;
} AssetPrefabTraitLifetimeDef;

typedef struct {
  f32    speed;
  f32    rotationSpeed; // Degrees per second.
  f32    radius, weight;
  String moveAnimation;
  bool   wheeled;
  f32    wheeledAcceleration;
} AssetPrefabTraitMovementDef;

typedef struct {
  String jointA, jointB;
  String decalIdA, decalIdB;
} AssetPrefabTraitFootstepDef;

typedef struct {
  f32    amount;
  f32    deathDestroyDelay;
  String deathEffectPrefab; // Optional, empty if unused.
} AssetPrefabTraitHealthDef;

typedef struct {
  String weaponId;
  String aimJoint;
  f32    aimSpeed; // Degrees per second.
  String aimSoundId;
  f32    targetRangeMin, targetRangeMax;
  f32    targetLineOfSightRadius;
  bool   targetExcludeUnreachable;
  bool   targetExcludeObscured;
} AssetPrefabTraitAttackDef;

typedef struct {
  bool                navBlocker;
  AssetPrefabShapeDef shape;
} AssetPrefabTraitCollisionDef;

typedef struct {
  String scriptId;
  struct {
    AssetPrefabValue* values;
    usize             count;
  } knowledge;
} AssetPrefabTraitScriptDef;

typedef struct {
  i32    priority;
  String barkDeathPrefab;   // Optional, empty if unused.
  String barkConfirmPrefab; // Optional, empty if unused.
} AssetPrefabTraitBarkDef;

typedef struct {
  AssetPrefabShapeBoxDef aimTarget;
} AssetPrefabTraitLocationDef;

typedef struct {
  String effectJoint;
  bool   burnable;
} AssetPrefabTraitStatusDef;

typedef struct {
  f32 radius;
} AssetPrefabTraitVisionDef;

typedef struct {
  AssetPrefabVec3Def spawnPos, rallyPos;
  String             rallySoundId;
  f32                rallySoundGain;
  String             productSetId;
  f32                placementRadius;
} AssetPrefabTraitProductionDef;

typedef struct {
  AssetPrefabTraitType type;
  union {
    AssetPrefabTraitNameDef         data_name;
    AssetPrefabTraitSetMemberDef    data_setMember;
    AssetPrefabTraitRenderableDef   data_renderable;
    AssetPrefabTraitVfxDef          data_vfx;
    AssetPrefabTraitDecalDef        data_decal;
    AssetPrefabTraitSoundDef        data_sound;
    AssetPrefabTraitLightPointDef   data_lightPoint;
    AssetPrefabTraitLightDirDef     data_lightDir;
    AssetPrefabTraitLightAmbientDef data_lightAmbient;
    AssetPrefabTraitLifetimeDef     data_lifetime;
    AssetPrefabTraitMovementDef     data_movement;
    AssetPrefabTraitFootstepDef     data_footstep;
    AssetPrefabTraitHealthDef       data_health;
    AssetPrefabTraitAttackDef       data_attack;
    AssetPrefabTraitCollisionDef    data_collision;
    AssetPrefabTraitScriptDef       data_script;
    AssetPrefabTraitBarkDef         data_bark;
    AssetPrefabTraitLocationDef     data_location;
    AssetPrefabTraitStatusDef       data_status;
    AssetPrefabTraitVisionDef       data_vision;
    AssetPrefabTraitProductionDef   data_production;
  };
} AssetPrefabTraitDef;

typedef struct {
  String name;
  bool   isVolatile;
  struct {
    AssetPrefabTraitDef* values;
    usize                count;
  } traits;
} AssetPrefabDef;

typedef struct {
  struct {
    AssetPrefabDef* values;
    usize           count;
  } prefabs;
} AssetPrefabMapDef;

static void prefab_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    DataReg* reg = data_reg_create(g_alloc_persist);

    prefab_set_flags_init();

    // clang-format off
    data_reg_struct_t(reg, AssetPrefabVec3Def);
    data_reg_field_t(reg, AssetPrefabVec3Def, x, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabVec3Def, y, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabVec3Def, z, data_prim_t(f32), .flags = DataFlags_Opt);

    data_reg_struct_t(reg, AssetPrefabColorDef);
    data_reg_field_t(reg, AssetPrefabColorDef, r, data_prim_t(f32));
    data_reg_field_t(reg, AssetPrefabColorDef, g, data_prim_t(f32));
    data_reg_field_t(reg, AssetPrefabColorDef, b, data_prim_t(f32));
    data_reg_field_t(reg, AssetPrefabColorDef, a, data_prim_t(f32));

    data_reg_struct_t(reg, AssetPrefabShapeSphereDef);
    data_reg_field_t(reg, AssetPrefabShapeSphereDef, offset, t_AssetPrefabVec3Def, .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabShapeSphereDef, radius, data_prim_t(f32), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(reg, AssetPrefabShapeCapsuleDef);
    data_reg_field_t(reg, AssetPrefabShapeCapsuleDef, offset, t_AssetPrefabVec3Def, .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabShapeCapsuleDef, radius, data_prim_t(f32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabShapeCapsuleDef, height, data_prim_t(f32), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(reg, AssetPrefabShapeBoxDef);
    data_reg_field_t(reg, AssetPrefabShapeBoxDef, min, t_AssetPrefabVec3Def);
    data_reg_field_t(reg, AssetPrefabShapeBoxDef, max, t_AssetPrefabVec3Def);

    data_reg_union_t(reg, AssetPrefabShapeDef, type);
    data_reg_choice_t(reg, AssetPrefabShapeDef, AssetPrefabShape_Sphere, data_sphere, t_AssetPrefabShapeSphereDef);
    data_reg_choice_t(reg, AssetPrefabShapeDef, AssetPrefabShape_Capsule, data_capsule, t_AssetPrefabShapeCapsuleDef);
    data_reg_choice_t(reg, AssetPrefabShapeDef, AssetPrefabShape_Box, data_box, t_AssetPrefabShapeBoxDef);

    data_reg_struct_t(reg, AssetPrefabValueSoundDef);
    data_reg_field_t(reg, AssetPrefabValueSoundDef, assetId, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabValueSoundDef, persistent, data_prim_t(bool), .flags = DataFlags_Opt);

    data_reg_union_t(reg, AssetPrefabValueDef, type);
    data_reg_union_name_t(reg, AssetPrefabValueDef, name);
    data_reg_choice_t(reg, AssetPrefabValueDef, AssetPrefabValue_Number, data_number, data_prim_t(f64));
    data_reg_choice_t(reg, AssetPrefabValueDef, AssetPrefabValue_Bool, data_bool, data_prim_t(bool));
    data_reg_choice_t(reg, AssetPrefabValueDef, AssetPrefabValue_Vector3, data_vector3, t_AssetPrefabVec3Def);
    data_reg_choice_t(reg, AssetPrefabValueDef, AssetPrefabValue_Color, data_color, t_AssetPrefabColorDef);
    data_reg_choice_t(reg, AssetPrefabValueDef, AssetPrefabValue_String, data_string, data_prim_t(String));
    data_reg_choice_t(reg, AssetPrefabValueDef, AssetPrefabValue_Asset, data_asset, data_prim_t(String));
    data_reg_choice_t(reg, AssetPrefabValueDef, AssetPrefabValue_Sound, data_sound, t_AssetPrefabValueSoundDef);

    data_reg_struct_t(reg, AssetPrefabTraitNameDef);
    data_reg_field_t(reg, AssetPrefabTraitNameDef, name, data_prim_t(String), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(reg, AssetPrefabTraitSetMemberDef);
    data_reg_field_t(reg, AssetPrefabTraitSetMemberDef, sets, data_prim_t(String), .container = DataContainer_Array, .flags = DataFlags_NotEmpty);

    data_reg_struct_t(reg, AssetPrefabTraitRenderableDef);
    data_reg_field_t(reg, AssetPrefabTraitRenderableDef, graphicId, data_prim_t(String), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(reg, AssetPrefabTraitVfxDef);
    data_reg_field_t(reg, AssetPrefabTraitVfxDef, assetId, data_prim_t(String), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(reg, AssetPrefabTraitDecalDef);
    data_reg_field_t(reg, AssetPrefabTraitDecalDef, assetId, data_prim_t(String), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(reg, AssetPrefabTraitSoundDef);
    data_reg_field_t(reg, AssetPrefabTraitSoundDef, assetIds, data_prim_t(String), .container = DataContainer_Array, .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitSoundDef, gainMin, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitSoundDef, gainMax, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitSoundDef, pitchMin, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitSoundDef, pitchMax, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitSoundDef, looping, data_prim_t(bool), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabTraitSoundDef, persistent, data_prim_t(bool), .flags = DataFlags_Opt);

    data_reg_struct_t(reg, AssetPrefabTraitLightPointDef);
    data_reg_field_t(reg, AssetPrefabTraitLightPointDef, radiance, t_AssetPrefabColorDef, .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitLightPointDef, radius, data_prim_t(f32), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(reg, AssetPrefabTraitLightDirDef);
    data_reg_field_t(reg, AssetPrefabTraitLightDirDef, radiance, t_AssetPrefabColorDef, .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitLightDirDef, shadows, data_prim_t(bool), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabTraitLightDirDef, coverage, data_prim_t(bool), .flags = DataFlags_Opt);

    data_reg_struct_t(reg, AssetPrefabTraitLightAmbientDef);
    data_reg_field_t(reg, AssetPrefabTraitLightAmbientDef, intensity, data_prim_t(f32), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(reg, AssetPrefabTraitLifetimeDef);
    data_reg_field_t(reg, AssetPrefabTraitLifetimeDef, duration, data_prim_t(f32), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(reg, AssetPrefabTraitMovementDef);
    data_reg_field_t(reg, AssetPrefabTraitMovementDef, speed, data_prim_t(f32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitMovementDef, rotationSpeed, data_prim_t(f32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitMovementDef, radius, data_prim_t(f32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitMovementDef, weight, data_prim_t(f32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitMovementDef, moveAnimation, data_prim_t(String), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabTraitMovementDef, wheeled, data_prim_t(bool), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabTraitMovementDef, wheeledAcceleration, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);

    data_reg_struct_t(reg, AssetPrefabTraitFootstepDef);
    data_reg_field_t(reg, AssetPrefabTraitFootstepDef, jointA, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitFootstepDef, jointB, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitFootstepDef, decalIdA, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitFootstepDef, decalIdB, data_prim_t(String), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(reg, AssetPrefabTraitHealthDef);
    data_reg_field_t(reg, AssetPrefabTraitHealthDef, amount, data_prim_t(f32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitHealthDef, deathDestroyDelay, data_prim_t(f32));
    data_reg_field_t(reg, AssetPrefabTraitHealthDef, deathEffectPrefab, data_prim_t(String), .flags = DataFlags_Opt | DataFlags_NotEmpty);

    data_reg_struct_t(reg, AssetPrefabTraitAttackDef);
    data_reg_field_t(reg, AssetPrefabTraitAttackDef, weaponId, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitAttackDef, aimJoint, data_prim_t(String), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitAttackDef, aimSpeed, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitAttackDef, aimSoundId, data_prim_t(String), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitAttackDef, targetRangeMin, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabTraitAttackDef, targetRangeMax, data_prim_t(f32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitAttackDef, targetLineOfSightRadius, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabTraitAttackDef, targetExcludeUnreachable, data_prim_t(bool), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabTraitAttackDef, targetExcludeObscured, data_prim_t(bool), .flags = DataFlags_Opt);

    data_reg_struct_t(reg, AssetPrefabTraitCollisionDef);
    data_reg_field_t(reg, AssetPrefabTraitCollisionDef, navBlocker, data_prim_t(bool));
    data_reg_field_t(reg, AssetPrefabTraitCollisionDef, shape, t_AssetPrefabShapeDef);

    data_reg_struct_t(reg, AssetPrefabTraitScriptDef);
    data_reg_field_t(reg, AssetPrefabTraitScriptDef, scriptId, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitScriptDef, knowledge, t_AssetPrefabValueDef, .container = DataContainer_Array, .flags = DataFlags_Opt);

    data_reg_struct_t(reg, AssetPrefabTraitBarkDef);
    data_reg_field_t(reg, AssetPrefabTraitBarkDef, priority, data_prim_t(i32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabTraitBarkDef, barkDeathPrefab, data_prim_t(String), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitBarkDef, barkConfirmPrefab, data_prim_t(String), .flags = DataFlags_Opt | DataFlags_NotEmpty);

    data_reg_struct_t(reg, AssetPrefabTraitLocationDef);
    data_reg_field_t(reg, AssetPrefabTraitLocationDef, aimTarget, t_AssetPrefabShapeBoxDef, .flags = DataFlags_Opt);

    data_reg_struct_t(reg, AssetPrefabTraitStatusDef);
    data_reg_field_t(reg, AssetPrefabTraitStatusDef, effectJoint, data_prim_t(String), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitStatusDef, burnable, data_prim_t(bool), .flags = DataFlags_Opt);

    data_reg_struct_t(reg, AssetPrefabTraitVisionDef);
    data_reg_field_t(reg, AssetPrefabTraitVisionDef, radius, data_prim_t(f32), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(reg, AssetPrefabTraitProductionDef);
    data_reg_field_t(reg, AssetPrefabTraitProductionDef, spawnPos, t_AssetPrefabVec3Def, .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabTraitProductionDef, rallyPos, t_AssetPrefabVec3Def, .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabTraitProductionDef, rallySoundId, data_prim_t(String), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabTraitProductionDef, rallySoundGain, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabTraitProductionDef, productSetId, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitProductionDef, placementRadius, data_prim_t(f32), .flags = DataFlags_Opt);

    data_reg_union_t(reg, AssetPrefabTraitDef, type);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Name, data_name, t_AssetPrefabTraitNameDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_SetMember, data_setMember, t_AssetPrefabTraitSetMemberDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Renderable, data_renderable, t_AssetPrefabTraitRenderableDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Vfx, data_vfx, t_AssetPrefabTraitVfxDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Decal, data_decal, t_AssetPrefabTraitDecalDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Sound, data_sound, t_AssetPrefabTraitSoundDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_LightPoint, data_lightPoint, t_AssetPrefabTraitLightPointDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_LightDir, data_lightDir, t_AssetPrefabTraitLightDirDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_LightAmbient, data_lightAmbient, t_AssetPrefabTraitLightAmbientDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Lifetime, data_lifetime, t_AssetPrefabTraitLifetimeDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Movement, data_movement, t_AssetPrefabTraitMovementDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Footstep, data_footstep, t_AssetPrefabTraitFootstepDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Health, data_health, t_AssetPrefabTraitHealthDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Attack, data_attack, t_AssetPrefabTraitAttackDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Collision, data_collision, t_AssetPrefabTraitCollisionDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Script, data_script, t_AssetPrefabTraitScriptDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Bark, data_bark, t_AssetPrefabTraitBarkDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Location, data_location, t_AssetPrefabTraitLocationDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Status, data_status, t_AssetPrefabTraitStatusDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Vision, data_vision, t_AssetPrefabTraitVisionDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Production, data_production, t_AssetPrefabTraitProductionDef);
    data_reg_choice_empty(reg, AssetPrefabTraitDef, AssetPrefabTrait_Scalable);

    data_reg_struct_t(reg, AssetPrefabDef);
    data_reg_field_t(reg, AssetPrefabDef, name, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabDef, isVolatile, data_prim_t(bool), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabDef, traits, t_AssetPrefabTraitDef, .container = DataContainer_Array);

    data_reg_struct_t(reg, AssetPrefabMapDef);
    data_reg_field_t(reg, AssetPrefabMapDef, prefabs, t_AssetPrefabDef, .container = DataContainer_Array);
    // clang-format on

    g_dataMapDefMeta = data_meta_t(t_AssetPrefabMapDef);
    g_dataReg        = reg;
  }
  thread_spinlock_unlock(&g_initLock);
}

static i8 prefab_compare(const void* a, const void* b) {
  return compare_stringhash(
      field_ptr(a, AssetPrefab, nameHash), field_ptr(b, AssetPrefab, nameHash));
}

typedef enum {
  PrefabError_None                      = 0,
  PrefabError_DuplicatePrefab           = 1,
  PrefabError_DuplicateTrait            = 2,
  PrefabError_PrefabCountExceedsMax     = 3,
  PrefabError_SetCountExceedsMax        = 4,
  PrefabError_SoundAssetCountExceedsMax = 5,

  PrefabError_Count,
} PrefabError;

static String prefab_error_str(const PrefabError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Multiple prefabs with the same name"),
      string_static("Prefab defines the same trait more then once"),
      string_static("Prefab count exceeds the maximum"),
      string_static("Set count exceeds the maximum"),
      string_static("Sound asset count exceeds the maximum"),
  };
  ASSERT(array_elems(g_msgs) == PrefabError_Count, "Incorrect number of error messages");
  return g_msgs[err];
}

typedef struct {
  EcsWorld*         world;
  AssetManagerComp* assetManager;
} BuildCtx;

static GeoVector prefab_build_vec3(const AssetPrefabVec3Def* def) {
  return geo_vector(def->x, def->y, def->z);
}

static GeoColor prefab_build_color(const AssetPrefabColorDef* def) {
  return geo_color(def->r, def->g, def->b, def->a);
}

static AssetPrefabShape prefab_build_shape(const AssetPrefabShapeDef* def) {
  switch (def->type) {
  case AssetPrefabShape_Sphere:
    return (AssetPrefabShape){
        .type               = AssetPrefabShape_Sphere,
        .data_sphere.offset = prefab_build_vec3(&def->data_sphere.offset),
        .data_sphere.radius = def->data_sphere.radius,
    };
    break;
  case AssetPrefabShape_Capsule:
    return (AssetPrefabShape){
        .type                = AssetPrefabShape_Capsule,
        .data_capsule.offset = prefab_build_vec3(&def->data_capsule.offset),
        .data_capsule.radius = def->data_capsule.radius,
        .data_capsule.height = def->data_capsule.height,
    };
    break;
  case AssetPrefabShape_Box:
    return (AssetPrefabShape){
        .type         = AssetPrefabShape_Box,
        .data_box.min = prefab_build_vec3(&def->data_box.min),
        .data_box.max = prefab_build_vec3(&def->data_box.max),
    };
    break;
  }
  diag_crash_msg("Unsupported prefab shape");
}

static AssetPrefabValue prefab_build_value(BuildCtx* ctx, const AssetPrefabValueDef* def) {
  AssetPrefabValue res;
  res.name = stringtable_add(g_stringtable, def->name);

  switch (def->type) {
  case AssetPrefabValue_Number:
    res.type        = AssetPrefabValue_Number;
    res.data_number = def->data_number;
    break;
  case AssetPrefabValue_Bool:
    res.type      = AssetPrefabValue_Bool;
    res.data_bool = def->data_bool;
    break;
  case AssetPrefabValue_Vector3:
    res.type         = AssetPrefabValue_Vector3;
    res.data_vector3 = prefab_build_vec3(&def->data_vector3);
    break;
  case AssetPrefabValue_Color:
    res.type       = AssetPrefabValue_Color;
    res.data_color = prefab_build_color(&def->data_color);
    break;
  case AssetPrefabValue_String:
    res.type        = AssetPrefabValue_String;
    res.data_string = stringtable_add(g_stringtable, def->data_string);
    break;
  case AssetPrefabValue_Asset:
    res.type       = AssetPrefabValue_Asset;
    res.data_asset = asset_lookup(ctx->world, ctx->assetManager, def->data_asset);
    break;
  case AssetPrefabValue_Sound:
    res.type             = AssetPrefabValue_Sound;
    res.data_sound.asset = asset_lookup(ctx->world, ctx->assetManager, def->data_sound.assetId);
    res.data_sound.persistent = def->data_sound.persistent;
    break;
  default:
    diag_crash_msg("Unsupported prefab value");
  }
  return res;
}

static AssetPrefabFlags prefab_build_flags(const AssetPrefabDef* def) {
  AssetPrefabFlags result = 0;
  result |= def->isVolatile ? AssetPrefabFlags_Volatile : 0;
  return result;
}

static void prefab_build(
    BuildCtx*             ctx,
    const AssetPrefabDef* def,
    DynArray*             outTraits, // AssetPrefabTrait[], needs to be already initialized.
    DynArray*             outValues, // AssetPrefabValue[], needs to be already initialized.
    AssetPrefab*          outPrefab,
    PrefabError*          err) {

  *err       = PrefabError_None;
  *outPrefab = (AssetPrefab){
      .nameHash   = stringtable_add(g_stringtable, def->name),
      .flags      = prefab_build_flags(def),
      .traitIndex = (u16)outTraits->size,
      .traitCount = (u16)def->traits.count,
  };

  const u8     addedTraitsBits[bits_to_bytes(AssetPrefabTrait_Count) + 1] = {0};
  const BitSet addedTraits = bitset_from_array(addedTraitsBits);

  AssetManagerComp* manager = ctx->assetManager;
  array_ptr_for_t(def->traits, AssetPrefabTraitDef, traitDef) {
    if (bitset_test(addedTraits, traitDef->type)) {
      *err = PrefabError_DuplicateTrait;
      return;
    }
    bitset_set(addedTraits, traitDef->type);

    AssetPrefabTrait* outTrait = dynarray_push_t(outTraits, AssetPrefabTrait);
    outTrait->type             = traitDef->type;

    switch (traitDef->type) {
    case AssetPrefabTrait_Name:
      outTrait->data_name = (AssetPrefabTraitName){
          .name = stringtable_add(g_stringtable, traitDef->data_name.name),
      };
      break;
    case AssetPrefabTrait_SetMember: {
      const AssetPrefabTraitSetMemberDef* setMemberDef = &traitDef->data_setMember;
      AssetPrefabTraitSetMember*          outSetMember = &outTrait->data_setMember;
      if (UNLIKELY(setMemberDef->sets.count > array_elems(outSetMember->sets))) {
        *err = PrefabError_SetCountExceedsMax;
        return;
      }
      *outSetMember = (AssetPrefabTraitSetMember){0};
      for (u32 i = 0; i != setMemberDef->sets.count; ++i) {
        outSetMember->sets[i] = stringtable_add(g_stringtable, setMemberDef->sets.values[i]);
        outPrefab->flags |= prefab_set_flags(outSetMember->sets[i]);
      }
    } break;
    case AssetPrefabTrait_Renderable:
      outTrait->data_renderable = (AssetPrefabTraitRenderable){
          .graphic = asset_lookup(ctx->world, manager, traitDef->data_renderable.graphicId),
      };
      break;
    case AssetPrefabTrait_Vfx:
      outTrait->data_vfx = (AssetPrefabTraitVfx){
          .asset = asset_lookup(ctx->world, manager, traitDef->data_vfx.assetId),
      };
      break;
    case AssetPrefabTrait_Decal:
      outTrait->data_decal = (AssetPrefabTraitDecal){
          .asset = asset_lookup(ctx->world, manager, traitDef->data_decal.assetId),
      };
      break;
    case AssetPrefabTrait_Sound: {
      const AssetPrefabTraitSoundDef* soundDef = &traitDef->data_sound;
      if (UNLIKELY(soundDef->assetIds.count > array_elems(outTrait->data_sound.assets))) {
        *err = PrefabError_SoundAssetCountExceedsMax;
        return;
      }
      const f32 gainMin    = soundDef->gainMin < f32_epsilon ? 1.0f : soundDef->gainMin;
      const f32 pitchMin   = soundDef->pitchMin < f32_epsilon ? 1.0f : soundDef->pitchMin;
      outTrait->data_sound = (AssetPrefabTraitSound){
          .gainMin    = gainMin,
          .gainMax    = math_max(gainMin, soundDef->gainMax),
          .pitchMin   = pitchMin,
          .pitchMax   = math_max(pitchMin, soundDef->pitchMax),
          .looping    = soundDef->looping,
          .persistent = soundDef->persistent,
      };
      for (u32 i = 0; i != soundDef->assetIds.count; ++i) {
        const EcsEntityId asset = asset_lookup(ctx->world, manager, soundDef->assetIds.values[i]);
        outTrait->data_sound.assets[i] = asset;
      }
      break;
    }
    case AssetPrefabTrait_LightPoint:
      outTrait->data_lightPoint = (AssetPrefabTraitLightPoint){
          .radiance = prefab_build_color(&traitDef->data_lightPoint.radiance),
          .radius   = math_max(0.01f, traitDef->data_lightPoint.radius),
      };
      break;
    case AssetPrefabTrait_LightDir:
      outTrait->data_lightDir = (AssetPrefabTraitLightDir){
          .radiance = prefab_build_color(&traitDef->data_lightDir.radiance),
          .shadows  = traitDef->data_lightDir.shadows,
          .coverage = traitDef->data_lightDir.coverage,
      };
      break;
    case AssetPrefabTrait_LightAmbient:
      outTrait->data_lightAmbient = (AssetPrefabTraitLightAmbient){
          .intensity = traitDef->data_lightAmbient.intensity,
      };
      break;
    case AssetPrefabTrait_Lifetime:
      outTrait->data_lifetime = (AssetPrefabTraitLifetime){
          .duration = (TimeDuration)time_seconds(traitDef->data_lifetime.duration),
      };
      break;
    case AssetPrefabTrait_Movement:
      outTrait->data_movement = (AssetPrefabTraitMovement){
          .speed            = traitDef->data_movement.speed,
          .rotationSpeedRad = traitDef->data_movement.rotationSpeed * math_deg_to_rad,
          .radius           = traitDef->data_movement.radius,
          .weight           = math_max(traitDef->data_movement.weight, trait_movement_weight_min),
          .moveAnimation    = string_maybe_hash(traitDef->data_movement.moveAnimation),
          .wheeled          = traitDef->data_movement.wheeled,
          .wheeledAcceleration = traitDef->data_movement.wheeledAcceleration,
      };
      break;
    case AssetPrefabTrait_Footstep:
      outTrait->data_footstep = (AssetPrefabTraitFootstep){
          .jointA      = stringtable_add(g_stringtable, traitDef->data_footstep.jointA),
          .jointB      = stringtable_add(g_stringtable, traitDef->data_footstep.jointB),
          .decalAssetA = asset_lookup(ctx->world, manager, traitDef->data_footstep.decalIdA),
          .decalAssetB = asset_lookup(ctx->world, manager, traitDef->data_footstep.decalIdB),
      };
      break;
    case AssetPrefabTrait_Health:
      outTrait->data_health = (AssetPrefabTraitHealth){
          .amount            = traitDef->data_health.amount,
          .deathDestroyDelay = (TimeDuration)time_seconds(traitDef->data_health.deathDestroyDelay),
          .deathEffectPrefab = string_maybe_hash(traitDef->data_health.deathEffectPrefab),
      };
      break;
    case AssetPrefabTrait_Attack:
      outTrait->data_attack = (AssetPrefabTraitAttack){
          .weapon      = string_hash(traitDef->data_attack.weaponId),
          .aimJoint    = string_maybe_hash(traitDef->data_attack.aimJoint),
          .aimSpeedRad = traitDef->data_attack.aimSpeed * math_deg_to_rad,
          .aimSoundAsset =
              asset_maybe_lookup(ctx->world, ctx->assetManager, traitDef->data_attack.aimSoundId),
          .targetRangeMin           = traitDef->data_attack.targetRangeMin,
          .targetRangeMax           = traitDef->data_attack.targetRangeMax,
          .targetLineOfSightRadius  = traitDef->data_attack.targetLineOfSightRadius,
          .targetExcludeUnreachable = traitDef->data_attack.targetExcludeUnreachable,
          .targetExcludeObscured    = traitDef->data_attack.targetExcludeObscured,
      };
      break;
    case AssetPrefabTrait_Collision:
      outTrait->data_collision = (AssetPrefabTraitCollision){
          .navBlocker = traitDef->data_collision.navBlocker,
          .shape      = prefab_build_shape(&traitDef->data_collision.shape),
      };
      break;
    case AssetPrefabTrait_Script:
      outTrait->data_script = (AssetPrefabTraitScript){
          .scriptAsset    = asset_lookup(ctx->world, manager, traitDef->data_script.scriptId),
          .knowledgeIndex = (u16)outValues->size,
          .knowledgeCount = (u16)traitDef->data_script.knowledge.count,
      };
      array_ptr_for_t(traitDef->data_script.knowledge, AssetPrefabValueDef, valDef) {
        *dynarray_push_t(outValues, AssetPrefabValue) = prefab_build_value(ctx, valDef);
      }
      break;
    case AssetPrefabTrait_Bark:
      outTrait->data_bark = (AssetPrefabTraitBark){
          .priority          = traitDef->data_bark.priority,
          .barkDeathPrefab   = string_maybe_hash(traitDef->data_bark.barkDeathPrefab),
          .barkConfirmPrefab = string_maybe_hash(traitDef->data_bark.barkConfirmPrefab),
      };
      break;
    case AssetPrefabTrait_Location:
      outTrait->data_location = (AssetPrefabTraitLocation){
          .aimTarget.min = prefab_build_vec3(&traitDef->data_location.aimTarget.min),
          .aimTarget.max = prefab_build_vec3(&traitDef->data_location.aimTarget.max),
      };
      break;
    case AssetPrefabTrait_Status:
      outTrait->data_status = (AssetPrefabTraitStatus){
          .effectJoint = string_maybe_hash(traitDef->data_status.effectJoint),
          .burnable    = traitDef->data_status.burnable,
      };
      break;
    case AssetPrefabTrait_Vision:
      outTrait->data_vision = (AssetPrefabTraitVision){
          .radius = traitDef->data_vision.radius,
      };
      break;
    case AssetPrefabTrait_Production: {
      const String rallySoundId   = traitDef->data_production.rallySoundId;
      const f32    rallySoundGain = traitDef->data_production.rallySoundGain;
      outTrait->data_production   = (AssetPrefabTraitProduction){
            .spawnPos        = prefab_build_vec3(&traitDef->data_production.spawnPos),
            .rallyPos        = prefab_build_vec3(&traitDef->data_production.rallyPos),
            .productSetId    = string_hash(traitDef->data_production.productSetId),
            .rallySoundAsset = asset_maybe_lookup(ctx->world, ctx->assetManager, rallySoundId),
            .rallySoundGain  = rallySoundGain <= 0 ? 1 : rallySoundGain,
            .placementRadius = traitDef->data_production.placementRadius,
      };
    } break;
    case AssetPrefabTrait_Scalable:
    case AssetPrefabTrait_Count:
      break;
    }
    if (*err) {
      return; // Failed to build trait.
    }
  }
}

static void prefabmap_build(
    BuildCtx*                ctx,
    const AssetPrefabMapDef* def,
    DynArray*                outPrefabs, // AssetPrefab[], needs to be already initialized.
    DynArray*                outTraits,  // AssetPrefabTrait[], needs to be already initialized.
    DynArray*                outValues,  // AssetPrefabValue[], needs to be already initialized.
    PrefabError*             err) {

  array_ptr_for_t(def->prefabs, AssetPrefabDef, prefabDef) {
    AssetPrefab prefab;
    prefab_build(ctx, prefabDef, outTraits, outValues, &prefab, err);
    if (*err) {
      return;
    }
    if (dynarray_search_binary(outPrefabs, prefab_compare, &prefab)) {
      *err = PrefabError_DuplicatePrefab;
      return;
    }
    *dynarray_insert_sorted_t(outPrefabs, AssetPrefab, prefab_compare, &prefab) = prefab;
  }
  *err = PrefabError_None;
}

/**
 * Build a lookup from the user-index (index in the source asset array) and the prefab index.
 */
static void prefabmap_build_user_index_lookup(
    const AssetPrefabMapDef* def,
    const AssetPrefab*       prefabs,           // AssetPrefab[def->prefabs.count]
    u16*                     outUserIndexLookup // u16[def->prefabs.count]
) {
  const u16          prefabCount = (u16)def->prefabs.count;
  const AssetPrefab* prefabsEnd  = prefabs + prefabCount;
  for (u16 userIndex = 0; userIndex != prefabCount; ++userIndex) {
    const StringHash   nameHash = string_hash(def->prefabs.values[userIndex].name);
    const AssetPrefab* prefab   = search_binary_t(
        prefabs, prefabsEnd, AssetPrefab, prefab_compare, &(AssetPrefab){.nameHash = nameHash});
    diag_assert(prefab && prefab->nameHash == nameHash);
    outUserIndexLookup[userIndex] = (u16)(prefab - prefabs);
  }
}

ecs_comp_define_public(AssetPrefabMapComp);
ecs_comp_define(AssetPrefabLoadComp) { AssetSource* src; };

static void ecs_destruct_prefabmap_comp(void* data) {
  AssetPrefabMapComp* comp = data;
  if (comp->prefabs) {
    alloc_free_array_t(g_alloc_heap, comp->prefabs, comp->prefabCount);
    alloc_free_array_t(g_alloc_heap, comp->userIndexLookup, comp->prefabCount);
  }
  if (comp->traits) {
    alloc_free_array_t(g_alloc_heap, comp->traits, comp->traitCount);
  }
  if (comp->values) {
    alloc_free_array_t(g_alloc_heap, comp->values, comp->valueCount);
  }
}

static void ecs_destruct_prefab_load_comp(void* data) {
  AssetPrefabLoadComp* comp = data;
  asset_repo_source_close(comp->src);
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(LoadView) { ecs_access_read(AssetPrefabLoadComp); }

ecs_view_define(UnloadView) {
  ecs_access_with(AssetPrefabMapComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Load prefab-map assets.
 */
ecs_system_define(LoadPrefabAssetSys) {
  AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
  if (!manager) {
    return;
  }

  EcsView* loadView = ecs_world_view_t(world, LoadView);
  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId  entity = ecs_view_entity(itr);
    const AssetSource* src    = ecs_view_read_t(itr, AssetPrefabLoadComp)->src;

    DynArray prefabs = dynarray_create_t(g_alloc_heap, AssetPrefab, 64);
    DynArray traits  = dynarray_create_t(g_alloc_heap, AssetPrefabTrait, 64);
    DynArray values  = dynarray_create_t(g_alloc_heap, AssetPrefabValue, 32);

    AssetPrefabMapDef def;
    String            errMsg;
    DataReadResult    readRes;
    data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataMapDefMeta, mem_var(def), &readRes);
    if (UNLIKELY(readRes.error)) {
      errMsg = readRes.errorMsg;
      goto Error;
    }
    if (UNLIKELY(def.prefabs.count > u16_max)) {
      errMsg = prefab_error_str(PrefabError_PrefabCountExceedsMax);
      goto Error;
    }

    BuildCtx buildCtx = {
        .world        = world,
        .assetManager = manager,
    };

    PrefabError buildErr;
    prefabmap_build(&buildCtx, &def, &prefabs, &traits, &values, &buildErr);
    if (buildErr) {
      errMsg = prefab_error_str(buildErr);
      goto Error;
    }

    u16* userIndexLookup = prefabs.size ? alloc_array_t(g_alloc_heap, u16, prefabs.size) : null;
    prefabmap_build_user_index_lookup(
        &def, dynarray_begin_t(&prefabs, AssetPrefab), userIndexLookup);

    ecs_world_add_t(
        world,
        entity,
        AssetPrefabMapComp,
        .prefabs         = dynarray_copy_as_new(&prefabs, g_alloc_heap),
        .userIndexLookup = userIndexLookup,
        .prefabCount     = prefabs.size,
        .traits          = dynarray_copy_as_new(&traits, g_alloc_heap),
        .traitCount      = traits.size,
        .values          = dynarray_copy_as_new(&values, g_alloc_heap),
        .valueCount      = values.size);

    ecs_world_add_empty_t(world, entity, AssetLoadedComp);
    goto Cleanup;

  Error:
    log_e("Failed to load PrefabMap", log_param("error", fmt_text(errMsg)));
    ecs_world_add_empty_t(world, entity, AssetFailedComp);

  Cleanup:
    data_destroy(g_dataReg, g_alloc_heap, g_dataMapDefMeta, mem_var(def));
    dynarray_destroy(&prefabs);
    dynarray_destroy(&traits);
    dynarray_destroy(&values);
    ecs_world_remove_t(world, entity, AssetPrefabLoadComp);
  }
}

/**
 * Remove any prefab-map asset component for unloaded assets.
 */
ecs_system_define(UnloadPrefabAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, UnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetPrefabMapComp);
  }
}

ecs_module_init(asset_prefab_module) {
  prefab_datareg_init();

  ecs_register_comp(AssetPrefabMapComp, .destructor = ecs_destruct_prefabmap_comp);
  ecs_register_comp(AssetPrefabLoadComp, .destructor = ecs_destruct_prefab_load_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(UnloadView);

  ecs_register_system(LoadPrefabAssetSys, ecs_view_id(ManagerView), ecs_view_id(LoadView));
  ecs_register_system(UnloadPrefabAssetSys, ecs_view_id(UnloadView));
}

void asset_load_prefabs(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;
  ecs_world_add_t(world, entity, AssetPrefabLoadComp, .src = src);
}

const AssetPrefab* asset_prefab_get(const AssetPrefabMapComp* map, const StringHash nameHash) {
  return search_binary_t(
      map->prefabs,
      map->prefabs + map->prefabCount,
      AssetPrefab,
      prefab_compare,
      mem_struct(AssetPrefab, .nameHash = nameHash).ptr);
}

u16 asset_prefab_get_index(const AssetPrefabMapComp* map, const StringHash nameHash) {
  const AssetPrefab* prefab = asset_prefab_get(map, nameHash);
  if (UNLIKELY(!prefab)) {
    return sentinel_u16;
  }
  return (u16)(prefab - map->prefabs);
}

u16 asset_prefab_get_index_from_user(const AssetPrefabMapComp* map, const u16 userIndex) {
  diag_assert(userIndex < map->prefabCount);
  return map->userIndexLookup[userIndex];
}

const AssetPrefabTrait* asset_prefab_trait_get(
    const AssetPrefabMapComp* map, const AssetPrefab* prefab, const AssetPrefabTraitType type) {
  for (u16 i = 0; i != prefab->traitCount; ++i) {
    const AssetPrefabTrait* trait = &map->traits[prefab->traitIndex + i];
    if (trait->type == type) {
      return trait;
    }
  }
  return null;
}

void asset_prefab_jsonschema_write(DynString* str) {
  prefab_datareg_init();

  const DataJsonSchemaFlags schemaFlags = DataJsonSchemaFlags_Compact;
  data_jsonschema_write(g_dataReg, str, g_dataMapDefMeta, schemaFlags);
}
