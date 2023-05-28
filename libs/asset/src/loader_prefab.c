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
#include "ecs_utils.h"
#include "log_logger.h"

#include "manager_internal.h"
#include "repo_internal.h"

static DataReg* g_dataReg;
static DataMeta g_dataMapDefMeta;

typedef struct {
  f32 x, y, z;
} AssetPrefabVec3Def;

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
  f32 duration;
} AssetPrefabTraitLifetimeDef;

typedef struct {
  f32    speed;
  f32    rotationSpeed; // Degrees per second.
  f32    radius;
  String moveAnimation;
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
  f32    targetDistanceMin, targetDistanceMax;
  f32    targetLineOfSightRadius;
  bool   targetExcludeUnreachable;
  bool   targetExcludeObscured;
} AssetPrefabTraitAttackDef;

typedef struct {
  bool                navBlocker;
  AssetPrefabShapeDef shape;
} AssetPrefabTraitCollisionDef;

typedef struct {
  String behaviorId;
} AssetPrefabTraitBrainDef;

typedef struct {
  String prefabId;
  f32    radius;
  u32    count;
  u32    maxInstances;
  f32    intervalMin, intervalMax;
} AssetPrefabTraitSpawnerDef;

typedef struct {
  f32    frequency;
  String effectPrefab; // Optional, empty if unused.
} AssetPrefabTraitBlinkDef;

typedef struct {
  i32    priority;
  String tauntDeathPrefab;   // Optional, empty if unused.
  String tauntConfirmPrefab; // Optional, empty if unused.
} AssetPrefabTraitTauntDef;

typedef struct {
  AssetPrefabVec3Def aimTarget;
} AssetPrefabTraitLocationDef;

typedef struct {
  f32 delay;
  f32 radius, damage;
} AssetPrefabTraitExplosiveDef;

typedef struct {
  String effectJoint;
  bool   burnable;
} AssetPrefabTraitStatusDef;

typedef struct {
  AssetPrefabTraitType type;
  union {
    AssetPrefabTraitRenderableDef data_renderable;
    AssetPrefabTraitVfxDef        data_vfx;
    AssetPrefabTraitDecalDef      data_decal;
    AssetPrefabTraitSoundDef      data_sound;
    AssetPrefabTraitLifetimeDef   data_lifetime;
    AssetPrefabTraitMovementDef   data_movement;
    AssetPrefabTraitFootstepDef   data_footstep;
    AssetPrefabTraitHealthDef     data_health;
    AssetPrefabTraitAttackDef     data_attack;
    AssetPrefabTraitCollisionDef  data_collision;
    AssetPrefabTraitBrainDef      data_brain;
    AssetPrefabTraitSpawnerDef    data_spawner;
    AssetPrefabTraitBlinkDef      data_blink;
    AssetPrefabTraitTauntDef      data_taunt;
    AssetPrefabTraitLocationDef   data_location;
    AssetPrefabTraitExplosiveDef  data_explosive;
    AssetPrefabTraitStatusDef     data_status;
  };
} AssetPrefabTraitDef;

typedef struct {
  String name;
  bool   isUnit;
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

    // clang-format off
    data_reg_struct_t(reg, AssetPrefabVec3Def);
    data_reg_field_t(reg, AssetPrefabVec3Def, x, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabVec3Def, y, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabVec3Def, z, data_prim_t(f32), .flags = DataFlags_Opt);

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

    data_reg_struct_t(reg, AssetPrefabTraitLifetimeDef);
    data_reg_field_t(reg, AssetPrefabTraitLifetimeDef, duration, data_prim_t(f32), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(reg, AssetPrefabTraitMovementDef);
    data_reg_field_t(reg, AssetPrefabTraitMovementDef, speed, data_prim_t(f32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitMovementDef, rotationSpeed, data_prim_t(f32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitMovementDef, radius, data_prim_t(f32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitMovementDef, moveAnimation, data_prim_t(String));

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
    data_reg_field_t(reg, AssetPrefabTraitAttackDef, targetDistanceMin, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabTraitAttackDef, targetDistanceMax, data_prim_t(f32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitAttackDef, targetLineOfSightRadius, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabTraitAttackDef, targetExcludeUnreachable, data_prim_t(bool), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabTraitAttackDef, targetExcludeObscured, data_prim_t(bool), .flags = DataFlags_Opt);

    data_reg_struct_t(reg, AssetPrefabTraitCollisionDef);
    data_reg_field_t(reg, AssetPrefabTraitCollisionDef, navBlocker, data_prim_t(bool));
    data_reg_field_t(reg, AssetPrefabTraitCollisionDef, shape, t_AssetPrefabShapeDef);

    data_reg_struct_t(reg, AssetPrefabTraitBrainDef);
    data_reg_field_t(reg, AssetPrefabTraitBrainDef, behaviorId, data_prim_t(String), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(reg, AssetPrefabTraitSpawnerDef);
    data_reg_field_t(reg, AssetPrefabTraitSpawnerDef, prefabId, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitSpawnerDef, radius, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabTraitSpawnerDef, count, data_prim_t(u32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitSpawnerDef, maxInstances, data_prim_t(u32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabTraitSpawnerDef, intervalMin, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabTraitSpawnerDef, intervalMax, data_prim_t(f32), .flags = DataFlags_Opt);

    data_reg_struct_t(reg, AssetPrefabTraitBlinkDef);
    data_reg_field_t(reg, AssetPrefabTraitBlinkDef, frequency, data_prim_t(f32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitBlinkDef, effectPrefab, data_prim_t(String), .flags = DataFlags_Opt | DataFlags_NotEmpty);

    data_reg_struct_t(reg, AssetPrefabTraitTauntDef);
    data_reg_field_t(reg, AssetPrefabTraitTauntDef, priority, data_prim_t(i32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabTraitTauntDef, tauntDeathPrefab, data_prim_t(String), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitTauntDef, tauntConfirmPrefab, data_prim_t(String), .flags = DataFlags_Opt | DataFlags_NotEmpty);

    data_reg_struct_t(reg, AssetPrefabTraitLocationDef);
    data_reg_field_t(reg, AssetPrefabTraitLocationDef, aimTarget, t_AssetPrefabVec3Def, .flags = DataFlags_Opt);

    data_reg_struct_t(reg, AssetPrefabTraitExplosiveDef);
    data_reg_field_t(reg, AssetPrefabTraitExplosiveDef, delay, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(reg, AssetPrefabTraitExplosiveDef, radius, data_prim_t(f32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitExplosiveDef, damage, data_prim_t(f32), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(reg, AssetPrefabTraitStatusDef);
    data_reg_field_t(reg, AssetPrefabTraitStatusDef, effectJoint, data_prim_t(String), .flags = DataFlags_Opt | DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabTraitStatusDef, burnable, data_prim_t(bool), .flags = DataFlags_Opt);

    data_reg_union_t(reg, AssetPrefabTraitDef, type);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Renderable, data_renderable, t_AssetPrefabTraitRenderableDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Vfx, data_vfx, t_AssetPrefabTraitVfxDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Decal, data_decal, t_AssetPrefabTraitDecalDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Sound, data_sound, t_AssetPrefabTraitSoundDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Lifetime, data_lifetime, t_AssetPrefabTraitLifetimeDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Movement, data_movement, t_AssetPrefabTraitMovementDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Footstep, data_footstep, t_AssetPrefabTraitFootstepDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Health, data_health, t_AssetPrefabTraitHealthDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Attack, data_attack, t_AssetPrefabTraitAttackDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Collision, data_collision, t_AssetPrefabTraitCollisionDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Brain, data_brain, t_AssetPrefabTraitBrainDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Spawner, data_spawner, t_AssetPrefabTraitSpawnerDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Blink, data_blink, t_AssetPrefabTraitBlinkDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Taunt, data_taunt, t_AssetPrefabTraitTauntDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Location, data_location, t_AssetPrefabTraitLocationDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Explosive, data_explosive, t_AssetPrefabTraitExplosiveDef);
    data_reg_choice_t(reg, AssetPrefabTraitDef, AssetPrefabTrait_Status, data_status, t_AssetPrefabTraitStatusDef);
    data_reg_choice_empty(reg, AssetPrefabTraitDef, AssetPrefabTrait_Scalable);

    data_reg_struct_t(reg, AssetPrefabDef);
    data_reg_field_t(reg, AssetPrefabDef, name, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetPrefabDef, isUnit, data_prim_t(bool), .flags = DataFlags_Opt);
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
  PrefabError_SoundAssetCountExceedsMax = 4,

  PrefabError_Count,
} PrefabError;

static String prefab_error_str(const PrefabError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Multiple prefabs with the same name"),
      string_static("Prefab defines the same trait more then once"),
      string_static("Prefab count exceeds the maximum"),
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

static AssetPrefabFlags prefab_build_flags(const AssetPrefabDef* def) {
  AssetPrefabFlags result = 0;
  result |= def->isUnit ? AssetPrefabFlags_Unit : 0;
  result |= def->isVolatile ? AssetPrefabFlags_Volatile : 0;
  return result;
}

static void prefab_build(
    BuildCtx*             ctx,
    const AssetPrefabDef* def,
    DynArray*             outTraits, // AssetPrefabTrait[], needs to be already initialized.
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
          .moveAnimation    = string_is_empty(traitDef->data_movement.moveAnimation)
                                  ? 0
                                  : string_hash(traitDef->data_movement.moveAnimation),
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
      outPrefab->flags |= AssetPrefabFlags_Destructible;
      break;
    case AssetPrefabTrait_Attack:
      outTrait->data_attack = (AssetPrefabTraitAttack){
          .weapon      = string_hash(traitDef->data_attack.weaponId),
          .aimJoint    = string_maybe_hash(traitDef->data_attack.aimJoint),
          .aimSpeedRad = traitDef->data_attack.aimSpeed * math_deg_to_rad,
          .aimSoundAsset =
              asset_maybe_lookup(ctx->world, ctx->assetManager, traitDef->data_attack.aimSoundId),
          .targetDistanceMin        = traitDef->data_attack.targetDistanceMin,
          .targetDistanceMax        = traitDef->data_attack.targetDistanceMax,
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
    case AssetPrefabTrait_Brain:
      outTrait->data_brain = (AssetPrefabTraitBrain){
          .behavior = asset_lookup(ctx->world, manager, traitDef->data_brain.behaviorId),
      };
      break;
    case AssetPrefabTrait_Spawner:
      outTrait->data_spawner = (AssetPrefabTraitSpawner){
          .prefabId     = string_hash(traitDef->data_spawner.prefabId),
          .radius       = traitDef->data_spawner.radius,
          .count        = traitDef->data_spawner.count,
          .maxInstances = traitDef->data_spawner.maxInstances,
          .intervalMin  = (TimeDuration)time_seconds(traitDef->data_spawner.intervalMin),
          .intervalMax  = (TimeDuration)time_seconds(traitDef->data_spawner.intervalMax),
      };
      break;
    case AssetPrefabTrait_Blink:
      outTrait->data_blink = (AssetPrefabTraitBlink){
          .frequency    = traitDef->data_blink.frequency,
          .effectPrefab = string_maybe_hash(traitDef->data_blink.effectPrefab),
      };
      break;
    case AssetPrefabTrait_Taunt:
      outTrait->data_taunt = (AssetPrefabTraitTaunt){
          .priority           = traitDef->data_taunt.priority,
          .tauntDeathPrefab   = string_maybe_hash(traitDef->data_taunt.tauntDeathPrefab),
          .tauntConfirmPrefab = string_maybe_hash(traitDef->data_taunt.tauntConfirmPrefab),
      };
      break;
    case AssetPrefabTrait_Location:
      outTrait->data_location = (AssetPrefabTraitLocation){
          .aimTarget = prefab_build_vec3(&traitDef->data_location.aimTarget),
      };
      break;
    case AssetPrefabTrait_Explosive:
      outTrait->data_explosive = (AssetPrefabTraitExplosive){
          .delay  = (TimeDuration)time_seconds(traitDef->data_explosive.delay),
          .radius = traitDef->data_explosive.radius,
          .damage = traitDef->data_explosive.damage,
      };
      break;
    case AssetPrefabTrait_Status:
      outTrait->data_status = (AssetPrefabTraitStatus){
          .effectJoint = string_maybe_hash(traitDef->data_status.effectJoint),
          .burnable    = traitDef->data_status.burnable,
      };
      break;
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
    PrefabError*             err) {

  array_ptr_for_t(def->prefabs, AssetPrefabDef, prefabDef) {
    AssetPrefab prefab;
    prefab_build(ctx, prefabDef, outTraits, &prefab, err);
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
    prefabmap_build(&buildCtx, &def, &prefabs, &traits, &buildErr);
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
        .traitCount      = traits.size);

    ecs_world_add_empty_t(world, entity, AssetLoadedComp);
    goto Cleanup;

  Error:
    log_e("Failed to load PrefabMap", log_param("error", fmt_text(errMsg)));
    ecs_world_add_empty_t(world, entity, AssetFailedComp);

  Cleanup:
    data_destroy(g_dataReg, g_alloc_heap, g_dataMapDefMeta, mem_var(def));
    dynarray_destroy(&prefabs);
    dynarray_destroy(&traits);
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

void asset_load_pfb(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
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
