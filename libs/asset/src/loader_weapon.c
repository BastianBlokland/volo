#include "asset_weapon.h"
#include "core_alloc.h"
#include "core_annotation.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_float.h"
#include "core_math.h"
#include "core_search.h"
#include "core_stringtable.h"
#include "data.h"
#include "data_schema.h"
#include "ecs_utils.h"
#include "log_logger.h"

#include "manager_internal.h"
#include "repo_internal.h"

DataMeta g_assetWeaponDataDef;

typedef struct {
  u32*  values;
  usize count;
} WeaponStatusMaskDef;

typedef struct {
  String              originJoint;
  bool                launchTowardsTarget, seekTowardsTarget;
  WeaponStatusMaskDef applyStatus;
  f32                 delay;
  f32                 spreadAngle;
  f32                 speed;
  f32                 damage, damageRadius;
  f32                 destroyDelay;
  String              projectilePrefab;
  String              impactPrefab; // Optional, empty if unused.
} AssetWeaponEffectProjDef;

typedef struct {
  bool                continuous;
  String              originJoint;
  f32                 delay;
  f32                 radius, radiusEnd;
  f32                 length;
  f32                 lengthGrowTime;
  f32                 damage;
  WeaponStatusMaskDef applyStatus;
  String              impactPrefab; // Optional, empty if unused.
} AssetWeaponEffectDmgDef;

typedef struct {
  bool   continuous, allowEarlyInterrupt;
  String layer;
  f32    delay;
  f32    speed;
  f32    durationMax;
} AssetWeaponEffectAnimDef;

typedef struct {
  String originJoint;
  f32    scale;
  bool   waitUntilFinished;
  f32    delay, duration;
  String assetId;
} AssetWeaponEffectVfxDef;

typedef struct {
  String originJoint;
  f32    delay, duration;
  String assetId;
  f32    gainMin, gainMax;
  f32    pitchMin, pitchMax;
} AssetWeaponEffectSoundDef;

typedef struct {
  AssetWeaponEffectType type;
  union {
    AssetWeaponEffectProjDef  data_proj;
    AssetWeaponEffectDmgDef   data_dmg;
    AssetWeaponEffectAnimDef  data_anim;
    AssetWeaponEffectVfxDef   data_vfx;
    AssetWeaponEffectSoundDef data_sound;
  };
} AssetWeaponEffectDef;

typedef struct {
  String name;
  f32    intervalMin, intervalMax;
  f32    readySpeed;
  bool   readyWhileMoving;
  f32    readyMinTime;
  String readyAnim;
  bool   predictiveAim;
  struct {
    AssetWeaponEffectDef* values;
    usize                 count;
  } effects;
} AssetWeaponDef;

typedef struct {
  struct {
    AssetWeaponDef* values;
    usize           count;
  } weapons;
} AssetWeaponMapDef;

static i8 asset_weapon_compare(const void* a, const void* b) {
  return compare_stringhash(
      field_ptr(a, AssetWeapon, nameHash), field_ptr(b, AssetWeapon, nameHash));
}

typedef enum {
  WeaponError_None                      = 0,
  WeaponError_DuplicateWeapon           = 1,
  WeaponError_OutOfBoundsAnimationSpeed = 2,

  WeaponError_Count,
} WeaponError;

static String weapon_error_str(const WeaponError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Multiple weapons with the same name"),
      string_static("Out of bounds animation speed"),
  };
  ASSERT(array_elems(g_msgs) == WeaponError_Count, "Incorrect number of error messages");
  return g_msgs[err];
}

typedef struct {
  EcsWorld*         world;
  AssetManagerComp* assetManager;
} BuildCtx;

static u8 weapon_status_mask_build(const WeaponStatusMaskDef* def) {
  u8 mask = 0;
  array_ptr_for_t(*def, u32, val) { mask |= *val; }
  return mask;
}

static void weapon_effect_proj_build(
    BuildCtx*                       ctx,
    const AssetWeaponEffectProjDef* def,
    AssetWeaponEffectProj*          out,
    WeaponError*                    err) {
  (void)ctx;
  *out = (AssetWeaponEffectProj){
      .originJoint         = string_hash(def->originJoint),
      .launchTowardsTarget = def->launchTowardsTarget,
      .seekTowardsTarget   = def->seekTowardsTarget,
      .applyStatusMask     = weapon_status_mask_build(&def->applyStatus),
      .delay               = (TimeDuration)time_seconds(def->delay),
      .spreadAngle         = def->spreadAngle,
      .speed               = def->speed,
      .damage              = def->damage,
      .damageRadius        = def->damageRadius,
      .destroyDelay        = (TimeDuration)time_seconds(def->destroyDelay),
      .projectilePrefab    = string_maybe_hash(def->projectilePrefab),
      .impactPrefab        = string_maybe_hash(def->impactPrefab),
  };
  *err = WeaponError_None;
}

static void weapon_effect_dmg_build(
    BuildCtx*                      ctx,
    const AssetWeaponEffectDmgDef* def,
    AssetWeaponEffectDmg*          out,
    WeaponError*                   err) {
  (void)ctx;
  *out = (AssetWeaponEffectDmg){
      .continuous      = def->continuous,
      .originJoint     = string_hash(def->originJoint),
      .delay           = (TimeDuration)time_seconds(def->delay),
      .damage          = def->damage,
      .radius          = def->radius,
      .radiusEnd       = def->radiusEnd,
      .length          = def->length,
      .lengthGrowTime  = (TimeDuration)time_seconds(def->lengthGrowTime),
      .applyStatusMask = weapon_status_mask_build(&def->applyStatus),
      .impactPrefab    = string_maybe_hash(def->impactPrefab),
  };
  *err = WeaponError_None;
}

static void weapon_effect_anim_build(
    BuildCtx*                       ctx,
    const AssetWeaponEffectAnimDef* def,
    AssetWeaponEffectAnim*          out,
    WeaponError*                    err) {
  (void)ctx;

  if (UNLIKELY(def->speed < 1e-4f || def->speed > 1e+4f)) {
    *err = WeaponError_OutOfBoundsAnimationSpeed;
    return;
  }
  *out = (AssetWeaponEffectAnim){
      .continuous          = def->continuous,
      .allowEarlyInterrupt = def->allowEarlyInterrupt,
      .layer               = string_hash(def->layer),
      .delay               = (TimeDuration)time_seconds(def->delay),
      .speed               = def->speed,
      .durationMax =
          def->durationMax <= 0 ? time_hour : (TimeDuration)time_seconds(def->durationMax),
  };
  *err = WeaponError_None;
}

static void weapon_effect_vfx_build(
    BuildCtx*                      ctx,
    const AssetWeaponEffectVfxDef* def,
    AssetWeaponEffectVfx*          out,
    WeaponError*                   err) {
  *out = (AssetWeaponEffectVfx){
      .originJoint       = string_hash(def->originJoint),
      .scale             = math_abs(def->scale) < f32_epsilon ? 1.0f : def->scale,
      .waitUntilFinished = def->waitUntilFinished,
      .delay             = (TimeDuration)time_seconds(def->delay),
      .duration          = (TimeDuration)time_seconds(def->duration),
      .asset             = asset_lookup(ctx->world, ctx->assetManager, def->assetId),
  };
  *err = WeaponError_None;
}

static void weapon_effect_sound_build(
    BuildCtx*                        ctx,
    const AssetWeaponEffectSoundDef* def,
    AssetWeaponEffectSound*          out,
    WeaponError*                     err) {
  const f32 gainMin  = def->gainMin < f32_epsilon ? 1.0f : def->gainMin;
  const f32 pitchMin = def->pitchMin < f32_epsilon ? 1.0f : def->pitchMin;

  *out = (AssetWeaponEffectSound){
      .originJoint = string_hash(def->originJoint),
      .delay       = (TimeDuration)time_seconds(def->delay),
      .duration    = (TimeDuration)time_seconds(def->duration),
      .asset       = asset_lookup(ctx->world, ctx->assetManager, def->assetId),
      .gainMin     = gainMin,
      .gainMax     = math_max(gainMin, def->gainMax),
      .pitchMin    = pitchMin,
      .pitchMax    = math_max(pitchMin, def->pitchMax),
  };
  *err = WeaponError_None;
}

static void weapon_build(
    BuildCtx*             ctx,
    const AssetWeaponDef* def,
    DynArray*             outEffects, // AssetWeaponEffect[], needs to be already initialized.
    AssetWeapon*          outWeapon,
    WeaponError*          err) {

  AssetWeaponFlags flags = 0;
  if (def->predictiveAim) {
    flags |= AssetWeapon_PredictiveAim;
  }

  *err       = WeaponError_None;
  *outWeapon = (AssetWeapon){
      .nameHash         = stringtable_add(g_stringtable, def->name),
      .flags            = flags,
      .intervalMin      = (TimeDuration)time_seconds(def->intervalMin),
      .intervalMax      = (TimeDuration)time_seconds(def->intervalMax),
      .readySpeed       = def->readySpeed,
      .readyWhileMoving = def->readyWhileMoving,
      .readyMinTime     = (TimeDuration)time_seconds(def->readyMinTime),
      .readyAnim        = string_is_empty(def->readyAnim) ? 0 : string_hash(def->readyAnim),
      .effectIndex      = (u16)outEffects->size,
      .effectCount      = (u16)def->effects.count,
  };

  array_ptr_for_t(def->effects, AssetWeaponEffectDef, effectDef) {
    AssetWeaponEffect* outEffect = dynarray_push_t(outEffects, AssetWeaponEffect);
    outEffect->type              = effectDef->type;

    switch (effectDef->type) {
    case AssetWeaponEffect_Projectile:
      weapon_effect_proj_build(ctx, &effectDef->data_proj, &outEffect->data_proj, err);
      break;
    case AssetWeaponEffect_Damage:
      weapon_effect_dmg_build(ctx, &effectDef->data_dmg, &outEffect->data_dmg, err);
      break;
    case AssetWeaponEffect_Animation:
      weapon_effect_anim_build(ctx, &effectDef->data_anim, &outEffect->data_anim, err);
      break;
    case AssetWeaponEffect_Vfx:
      weapon_effect_vfx_build(ctx, &effectDef->data_vfx, &outEffect->data_vfx, err);
      break;
    case AssetWeaponEffect_Sound:
      weapon_effect_sound_build(ctx, &effectDef->data_sound, &outEffect->data_sound, err);
      break;
    }
    if (*err) {
      return; // Failed to build effect.
    }
  }
}

static void weaponmap_build(
    BuildCtx*                ctx,
    const AssetWeaponMapDef* def,
    DynArray*                outWeapons, // AssetWeapon[], needs to be already initialized.
    DynArray*                outEffects, // AssetWeaponEffect[], needs to be already initialized.
    WeaponError*             err) {

  array_ptr_for_t(def->weapons, AssetWeaponDef, weaponDef) {
    AssetWeapon weapon;
    weapon_build(ctx, weaponDef, outEffects, &weapon, err);
    if (*err) {
      return;
    }
    if (dynarray_search_binary(outWeapons, asset_weapon_compare, &weapon)) {
      *err = WeaponError_DuplicateWeapon;
      return;
    }
    *dynarray_insert_sorted_t(outWeapons, AssetWeapon, asset_weapon_compare, &weapon) = weapon;
  }
  *err = WeaponError_None;
}

ecs_comp_define_public(AssetWeaponMapComp);
ecs_comp_define(AssetWeaponLoadComp) { AssetSource* src; };

static void ecs_destruct_weaponmap_comp(void* data) {
  AssetWeaponMapComp* comp = data;
  if (comp->weapons) {
    alloc_free_array_t(g_allocHeap, comp->weapons, comp->weaponCount);
  }
  if (comp->effects) {
    alloc_free_array_t(g_allocHeap, comp->effects, comp->effectCount);
  }
}

static void ecs_destruct_weapon_load_comp(void* data) {
  AssetWeaponLoadComp* comp = data;
  asset_repo_source_close(comp->src);
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }

ecs_view_define(LoadView) {
  ecs_access_read(AssetComp);
  ecs_access_read(AssetWeaponLoadComp);
}

ecs_view_define(UnloadView) {
  ecs_access_with(AssetWeaponMapComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Load weapon-map assets.
 */
ecs_system_define(LoadWeaponAssetSys) {
  AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
  if (!manager) {
    return;
  }

  EcsView* loadView = ecs_world_view_t(world, LoadView);
  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId  entity = ecs_view_entity(itr);
    const String       id     = asset_id(ecs_view_read_t(itr, AssetComp));
    const AssetSource* src    = ecs_view_read_t(itr, AssetWeaponLoadComp)->src;

    DynArray weapons = dynarray_create_t(g_allocHeap, AssetWeapon, 64);
    DynArray effects = dynarray_create_t(g_allocHeap, AssetWeaponEffect, 64);

    AssetWeaponMapDef def;
    String            errMsg;
    DataReadResult    readRes;
    data_read_json(g_dataReg, src->data, g_allocHeap, g_assetWeaponDataDef, mem_var(def), &readRes);
    if (UNLIKELY(readRes.error)) {
      errMsg = readRes.errorMsg;
      goto Error;
    }

    BuildCtx buildCtx = {
        .world        = world,
        .assetManager = manager,
    };

    WeaponError buildErr;
    weaponmap_build(&buildCtx, &def, &weapons, &effects, &buildErr);
    data_destroy(g_dataReg, g_allocHeap, g_assetWeaponDataDef, mem_var(def));
    if (buildErr) {
      errMsg = weapon_error_str(buildErr);
      goto Error;
    }

    ecs_world_add_t(
        world,
        entity,
        AssetWeaponMapComp,
        .weapons     = dynarray_copy_as_new(&weapons, g_allocHeap),
        .weaponCount = weapons.size,
        .effects     = dynarray_copy_as_new(&effects, g_allocHeap),
        .effectCount = effects.size);

    ecs_world_add_empty_t(world, entity, AssetLoadedComp);
    goto Cleanup;

  Error:
    log_e(
        "Failed to load WeaponMap",
        log_param("id", fmt_text(id)),
        log_param("error", fmt_text(errMsg)));
    ecs_world_add_empty_t(world, entity, AssetFailedComp);

  Cleanup:
    dynarray_destroy(&weapons);
    dynarray_destroy(&effects);
    ecs_world_remove_t(world, entity, AssetWeaponLoadComp);
  }
}

/**
 * Remove any weapon-map asset component for unloaded assets.
 */
ecs_system_define(UnloadWeaponAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, UnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetWeaponMapComp);
  }
}

ecs_module_init(asset_weapon_module) {
  ecs_register_comp(AssetWeaponMapComp, .destructor = ecs_destruct_weaponmap_comp);
  ecs_register_comp(AssetWeaponLoadComp, .destructor = ecs_destruct_weapon_load_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(UnloadView);

  ecs_register_system(LoadWeaponAssetSys, ecs_view_id(ManagerView), ecs_view_id(LoadView));
  ecs_register_system(UnloadWeaponAssetSys, ecs_view_id(UnloadView));
}

void asset_data_init_weapon(void) {
  // clang-format off
  /**
    * Status indices correspond to the 'SceneStatusType' values as defined in 'scene_status.h'.
    * NOTE: Unfortunately we cannot reference the SceneStatusType enum directly as that would
    * require an undesired dependency on the scene library.
    * NOTE: This is a virtual data type, meaning there is no matching AssetWeaponStatusMask C type.
    */
  data_reg_enum_t(g_dataReg, AssetWeaponStatusMask);
  data_reg_const_custom(g_dataReg, AssetWeaponStatusMask, Burning,  1 << 0);
  data_reg_const_custom(g_dataReg, AssetWeaponStatusMask, Bleeding, 1 << 1);
  data_reg_const_custom(g_dataReg, AssetWeaponStatusMask, Healing,  1 << 2);
  data_reg_const_custom(g_dataReg, AssetWeaponStatusMask, Veteran,  1 << 3);

  data_reg_struct_t(g_dataReg, AssetWeaponEffectProjDef);
  data_reg_field_t(g_dataReg, AssetWeaponEffectProjDef, originJoint, data_prim_t(String), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectProjDef, launchTowardsTarget, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponEffectProjDef, seekTowardsTarget, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponEffectProjDef, applyStatus, t_AssetWeaponStatusMask, .container = DataContainer_Array, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponEffectProjDef, delay, data_prim_t(f32));
  data_reg_field_t(g_dataReg, AssetWeaponEffectProjDef, spreadAngle, data_prim_t(f32));
  data_reg_field_t(g_dataReg, AssetWeaponEffectProjDef, speed, data_prim_t(f32), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectProjDef, damage, data_prim_t(f32), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectProjDef, damageRadius, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponEffectProjDef, destroyDelay, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponEffectProjDef, projectilePrefab, data_prim_t(String), .flags = DataFlags_NotEmpty | DataFlags_Intern);
  data_reg_field_t(g_dataReg, AssetWeaponEffectProjDef, impactPrefab, data_prim_t(String), .flags = DataFlags_Opt | DataFlags_NotEmpty | DataFlags_Intern);

  data_reg_struct_t(g_dataReg, AssetWeaponEffectDmgDef);
  data_reg_field_t(g_dataReg, AssetWeaponEffectDmgDef, continuous, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponEffectDmgDef, originJoint, data_prim_t(String), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectDmgDef, delay, data_prim_t(f32));
  data_reg_field_t(g_dataReg, AssetWeaponEffectDmgDef, radius, data_prim_t(f32), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectDmgDef, radiusEnd, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectDmgDef, length, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponEffectDmgDef, lengthGrowTime, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponEffectDmgDef, damage, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectDmgDef, applyStatus, t_AssetWeaponStatusMask, .container = DataContainer_Array, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponEffectDmgDef, impactPrefab, data_prim_t(String), .flags = DataFlags_Opt | DataFlags_NotEmpty | DataFlags_Intern);

  data_reg_struct_t(g_dataReg, AssetWeaponEffectAnimDef);
  data_reg_field_t(g_dataReg, AssetWeaponEffectAnimDef, continuous, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponEffectAnimDef, allowEarlyInterrupt, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponEffectAnimDef, layer, data_prim_t(String), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectAnimDef, delay, data_prim_t(f32));
  data_reg_field_t(g_dataReg, AssetWeaponEffectAnimDef, speed, data_prim_t(f32), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectAnimDef, durationMax, data_prim_t(f32), .flags = DataFlags_Opt);

  data_reg_struct_t(g_dataReg, AssetWeaponEffectVfxDef);
  data_reg_field_t(g_dataReg, AssetWeaponEffectVfxDef, assetId, data_prim_t(String), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectVfxDef, scale, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectVfxDef, waitUntilFinished, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponEffectVfxDef, delay, data_prim_t(f32));
  data_reg_field_t(g_dataReg, AssetWeaponEffectVfxDef, duration, data_prim_t(f32));
  data_reg_field_t(g_dataReg, AssetWeaponEffectVfxDef, originJoint, data_prim_t(String), .flags = DataFlags_NotEmpty);

  data_reg_struct_t(g_dataReg, AssetWeaponEffectSoundDef);
  data_reg_field_t(g_dataReg, AssetWeaponEffectSoundDef, assetId, data_prim_t(String), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectSoundDef, delay, data_prim_t(f32));
  data_reg_field_t(g_dataReg, AssetWeaponEffectSoundDef, duration, data_prim_t(f32));
  data_reg_field_t(g_dataReg, AssetWeaponEffectSoundDef, originJoint, data_prim_t(String), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectSoundDef, gainMin, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectSoundDef, gainMax, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectSoundDef, pitchMin, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectSoundDef, pitchMax, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);

  data_reg_union_t(g_dataReg, AssetWeaponEffectDef, type);
  data_reg_choice_t(g_dataReg, AssetWeaponEffectDef, AssetWeaponEffect_Projectile, data_proj, t_AssetWeaponEffectProjDef);
  data_reg_choice_t(g_dataReg, AssetWeaponEffectDef, AssetWeaponEffect_Damage, data_dmg, t_AssetWeaponEffectDmgDef);
  data_reg_choice_t(g_dataReg, AssetWeaponEffectDef, AssetWeaponEffect_Animation, data_anim, t_AssetWeaponEffectAnimDef);
  data_reg_choice_t(g_dataReg, AssetWeaponEffectDef, AssetWeaponEffect_Vfx, data_vfx, t_AssetWeaponEffectVfxDef);
  data_reg_choice_t(g_dataReg, AssetWeaponEffectDef, AssetWeaponEffect_Sound, data_sound, t_AssetWeaponEffectSoundDef);

  data_reg_struct_t(g_dataReg, AssetWeaponDef);
  data_reg_field_t(g_dataReg, AssetWeaponDef, name, data_prim_t(String), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponDef, intervalMin, data_prim_t(f32));
  data_reg_field_t(g_dataReg, AssetWeaponDef, intervalMax, data_prim_t(f32));
  data_reg_field_t(g_dataReg, AssetWeaponDef, readySpeed, data_prim_t(f32));
  data_reg_field_t(g_dataReg, AssetWeaponDef, readyWhileMoving, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponDef, readyMinTime, data_prim_t(f32));
  data_reg_field_t(g_dataReg, AssetWeaponDef, readyAnim, data_prim_t(String), .flags = DataFlags_NotEmpty | DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponDef, predictiveAim, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponDef, effects, t_AssetWeaponEffectDef, .container = DataContainer_Array);

  data_reg_struct_t(g_dataReg, AssetWeaponMapDef);
  data_reg_field_t(g_dataReg, AssetWeaponMapDef, weapons, t_AssetWeaponDef, .container = DataContainer_Array);
  // clang-format on

  g_assetWeaponDataDef = data_meta_t(t_AssetWeaponMapDef);
}

void asset_load_weapons(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;
  ecs_world_add_t(world, entity, AssetWeaponLoadComp, .src = src);
}

f32 asset_weapon_damage(const AssetWeaponMapComp* map, const AssetWeapon* weapon) {
  f32 damage = 0;
  for (u16 i = 0; i != weapon->effectCount; ++i) {
    const AssetWeaponEffect* effect = &map->effects[weapon->effectIndex + i];
    switch (effect->type) {
    case AssetWeaponEffect_Projectile:
      damage += effect->data_proj.damage;
      break;
    case AssetWeaponEffect_Damage:
      damage += effect->data_dmg.damage;
      break;
    case AssetWeaponEffect_Animation:
    case AssetWeaponEffect_Vfx:
    case AssetWeaponEffect_Sound:
      break;
    }
  }
  return damage;
}

u8 asset_weapon_applies_status(const AssetWeaponMapComp* map, const AssetWeapon* weapon) {
  u8 result = 0;
  for (u16 i = 0; i != weapon->effectCount; ++i) {
    const AssetWeaponEffect* effect = &map->effects[weapon->effectIndex + i];
    switch (effect->type) {
    case AssetWeaponEffect_Damage:
      result |= effect->data_dmg.applyStatusMask;
      break;
    case AssetWeaponEffect_Projectile:
      result |= effect->data_proj.applyStatusMask;
      break;
    case AssetWeaponEffect_Animation:
    case AssetWeaponEffect_Vfx:
    case AssetWeaponEffect_Sound:
      break;
    }
  }
  return result;
}

const AssetWeapon* asset_weapon_get(const AssetWeaponMapComp* map, const StringHash nameHash) {
  return search_binary_t(
      map->weapons,
      map->weapons + map->weaponCount,
      AssetWeapon,
      asset_weapon_compare,
      mem_struct(AssetWeapon, .nameHash = nameHash).ptr);
}

void asset_weapon_jsonschema_write(DynString* str) {
  const DataJsonSchemaFlags schemaFlags = DataJsonSchemaFlags_Compact;
  data_jsonschema_write(g_dataReg, str, g_assetWeaponDataDef, schemaFlags);
}
