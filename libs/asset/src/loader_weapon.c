#include "asset_weapon.h"
#include "core_alloc.h"
#include "core_dynarray.h"
#include "core_float.h"
#include "core_math.h"
#include "core_search.h"
#include "core_time.h"
#include "data_read.h"
#include "data_utils.h"
#include "ecs_entity.h"
#include "ecs_utils.h"
#include "ecs_view.h"

#include "data_internal.h"
#include "manager_internal.h"
#include "repo_internal.h"

DataMeta g_assetWeaponDefMeta;

typedef struct {
  StringHash   name;
  TimeDuration intervalMin, intervalMax;
  f32          readySpeed;
  bool         readyWhileMoving;
  TimeDuration readyMinTime;
  StringHash   readyAnim;
  bool         predictiveAim;
  HeapArray_t(AssetWeaponEffect) effects;
} AssetWeaponDef;

typedef struct {
  HeapArray_t(AssetWeaponDef) weapons;
} AssetWeaponMapDef;

static i8 asset_weapon_compare(const void* a, const void* b) {
  return compare_stringhash(field_ptr(a, AssetWeapon, name), field_ptr(b, AssetWeapon, name));
}

typedef enum {
  WeaponError_None,
  WeaponError_DuplicateWeapon,
  WeaponError_InvalidAssetReference,

  WeaponError_Count,
} WeaponError;

static String weapon_error_str(const WeaponError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Multiple weapons with the same name"),
      string_static("Unable to resolve asset-reference"),
  };
  ASSERT(array_elems(g_msgs) == WeaponError_Count, "Incorrect number of error messages");
  return g_msgs[err];
}

static void weapon_build(
    const AssetWeaponDef* def,
    DynArray*             outEffects, // AssetWeaponEffect[], needs to be already initialized.
    AssetWeapon*          outWeapon) {

  AssetWeaponFlags flags = 0;
  if (def->predictiveAim) {
    flags |= AssetWeapon_PredictiveAim;
  }
  *outWeapon = (AssetWeapon){
      .name             = def->name,
      .flags            = flags,
      .intervalMin      = def->intervalMin,
      .intervalMax      = def->intervalMax,
      .readySpeed       = def->readySpeed,
      .readyWhileMoving = def->readyWhileMoving,
      .readyMinTime     = def->readyMinTime,
      .readyAnim        = def->readyAnim,
      .effectIndex      = (u16)outEffects->size,
      .effectCount      = (u16)def->effects.count,
  };
  mem_cpy(
      dynarray_push(outEffects, def->effects.count),
      mem_create(def->effects.values, sizeof(AssetWeaponEffect) * def->effects.count));
}

static void weaponmap_build(
    const AssetWeaponMapDef* def,
    DynArray*                outWeapons, // AssetWeapon[], needs to be already initialized.
    DynArray*                outEffects, // AssetWeaponEffect[], needs to be already initialized.
    WeaponError*             err) {
  *err = WeaponError_None;
  heap_array_for_t(def->weapons, AssetWeaponDef, weaponDef) {
    AssetWeapon weapon;
    weapon_build(weaponDef, outEffects, &weapon);
    if (*err) {
      return;
    }
    if (dynarray_search_binary(outWeapons, asset_weapon_compare, &weapon)) {
      *err = WeaponError_DuplicateWeapon;
      return;
    }
    *dynarray_insert_sorted_t(outWeapons, AssetWeapon, asset_weapon_compare, &weapon) = weapon;
  }
}

ecs_comp_define_public(AssetWeaponMapComp);
ecs_comp_define(AssetWeaponLoadComp) { AssetWeaponMapDef def; };

static void ecs_destruct_weaponmap_comp(void* data) {
  AssetWeaponMapComp* comp = data;
  if (comp->weapons.values) {
    alloc_free_array_t(g_allocHeap, comp->weapons.values, comp->weapons.count);
  }
  if (comp->effects.values) {
    alloc_free_array_t(g_allocHeap, comp->effects.values, comp->effects.count);
  }
}

static void ecs_destruct_weapon_load_comp(void* data) {
  AssetWeaponLoadComp* comp = data;
  data_destroy(g_dataReg, g_allocHeap, g_assetWeaponDefMeta, mem_var(comp->def));
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }

ecs_view_define(LoadView) {
  ecs_access_read(AssetComp);
  ecs_access_write(AssetWeaponLoadComp);
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
    const EcsEntityId    entity = ecs_view_entity(itr);
    const String         id     = asset_id(ecs_view_read_t(itr, AssetComp));
    AssetWeaponLoadComp* load   = ecs_view_write_t(itr, AssetWeaponLoadComp);
    const DataMeta       meta   = g_assetWeaponDefMeta;

    DynArray weapons = dynarray_create_t(g_allocHeap, AssetWeapon, 64);
    DynArray effects = dynarray_create_t(g_allocHeap, AssetWeaponEffect, 64);

    String errMsg;
    if (UNLIKELY(!asset_data_patch_refs(world, manager, meta, mem_var(load->def)))) {
      errMsg = weapon_error_str(WeaponError_InvalidAssetReference);
      goto Error;
    }

    WeaponError buildErr;
    weaponmap_build(&load->def, &weapons, &effects, &buildErr);
    if (buildErr) {
      errMsg = weapon_error_str(buildErr);
      goto Error;
    }

    ecs_world_add_t(
        world,
        entity,
        AssetWeaponMapComp,
        .weapons.values = dynarray_copy_as_new(&weapons, g_allocHeap),
        .weapons.count  = weapons.size,
        .effects.values = dynarray_copy_as_new(&effects, g_allocHeap),
        .effects.count  = effects.size);

    asset_mark_load_success(world, entity);
    goto Cleanup;

  Error:
    asset_mark_load_failure(world, entity, id, errMsg, -1 /* errorCode */);

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

static bool weapon_data_normalizer_effect_anim(const Mem data) {
  AssetWeaponEffectAnim* effect = mem_as_t(data, AssetWeaponEffectAnim);
  if (UNLIKELY(effect->speed < 1e-4f || effect->speed > 1e+4f)) {
    return false;
  }
  return true;
}

static bool weapon_data_normalizer_effect_vfx(const Mem data) {
  AssetWeaponEffectVfx* effect = mem_as_t(data, AssetWeaponEffectVfx);
  effect->scale                = math_abs(effect->scale) < f32_epsilon ? 1.0f : effect->scale;
  return true;
}

static bool weapon_data_normalizer_effect_sound(const Mem data) {
  AssetWeaponEffectSound* effect = mem_as_t(data, AssetWeaponEffectSound);
  effect->gainMin                = effect->gainMin < f32_epsilon ? 1.0f : effect->gainMin;
  effect->pitchMin               = effect->pitchMin < f32_epsilon ? 1.0f : effect->pitchMin;
  effect->gainMax                = math_max(effect->gainMin, effect->gainMax);
  effect->pitchMax               = math_max(effect->pitchMin, effect->pitchMax);
  return true;
}

void asset_data_init_weapon(void) {
  // clang-format off
  /**
    * Status indices correspond to the 'SceneStatusType' values as defined in 'scene_status.h'.
    * NOTE: Unfortunately we cannot reference the SceneStatusType enum directly as that would
    * require an undesired dependency on the scene library.
    * NOTE: This is a virtual data type, meaning there is no matching AssetWeaponStatusMask C type.
    */
  data_reg_enum_multi_t(g_dataReg, AssetWeaponStatusMask);
  data_reg_const_custom(g_dataReg, AssetWeaponStatusMask, Burning,  1 << 0);
  data_reg_const_custom(g_dataReg, AssetWeaponStatusMask, Bleeding, 1 << 1);
  data_reg_const_custom(g_dataReg, AssetWeaponStatusMask, Healing,  1 << 2);
  data_reg_const_custom(g_dataReg, AssetWeaponStatusMask, Veteran,  1 << 3);

  data_reg_struct_t(g_dataReg, AssetWeaponEffectProj);
  data_reg_field_t(g_dataReg, AssetWeaponEffectProj, originJoint, data_prim_t(StringHash), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectProj, launchTowardsTarget, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponEffectProj, seekTowardsTarget, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponEffectProj, applyStatus, t_AssetWeaponStatusMask, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponEffectProj, delay, data_prim_t(TimeDuration));
  data_reg_field_t(g_dataReg, AssetWeaponEffectProj, spreadAngle, data_prim_t(f32));
  data_reg_field_t(g_dataReg, AssetWeaponEffectProj, speed, data_prim_t(f32), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectProj, damage, data_prim_t(f32), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectProj, damageRadius, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponEffectProj, destroyDelay, data_prim_t(TimeDuration), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponEffectProj, projectilePrefab, data_prim_t(StringHash), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectProj, impactPrefab, data_prim_t(StringHash), .flags = DataFlags_Opt | DataFlags_NotEmpty);

  data_reg_struct_t(g_dataReg, AssetWeaponEffectDmg);
  data_reg_field_t(g_dataReg, AssetWeaponEffectDmg, continuous, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponEffectDmg, originJoint, data_prim_t(StringHash), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectDmg, delay, data_prim_t(TimeDuration));
  data_reg_field_t(g_dataReg, AssetWeaponEffectDmg, radius, data_prim_t(f32), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectDmg, radiusEnd, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectDmg, length, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponEffectDmg, lengthGrowTime, data_prim_t(TimeDuration), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponEffectDmg, damage, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectDmg, applyStatus, t_AssetWeaponStatusMask, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponEffectDmg, impactPrefab, data_prim_t(StringHash), .flags = DataFlags_Opt | DataFlags_NotEmpty);

  data_reg_struct_t(g_dataReg, AssetWeaponEffectAnim);
  data_reg_field_t(g_dataReg, AssetWeaponEffectAnim, continuous, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponEffectAnim, allowEarlyInterrupt, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponEffectAnim, layer, data_prim_t(StringHash), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectAnim, delay, data_prim_t(TimeDuration));
  data_reg_field_t(g_dataReg, AssetWeaponEffectAnim, speed, data_prim_t(f32), .flags = DataFlags_NotEmpty);
  data_reg_normalizer_t(g_dataReg, AssetWeaponEffectAnim, weapon_data_normalizer_effect_anim);

  data_reg_struct_t(g_dataReg, AssetWeaponEffectVfx);
  data_reg_field_t(g_dataReg, AssetWeaponEffectVfx, asset, g_assetRefType);
  data_reg_field_t(g_dataReg, AssetWeaponEffectVfx, scale, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectVfx, waitUntilFinished, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponEffectVfx, delay, data_prim_t(TimeDuration));
  data_reg_field_t(g_dataReg, AssetWeaponEffectVfx, duration, data_prim_t(TimeDuration));
  data_reg_field_t(g_dataReg, AssetWeaponEffectVfx, originJoint, data_prim_t(StringHash), .flags = DataFlags_NotEmpty);
  data_reg_normalizer_t(g_dataReg, AssetWeaponEffectVfx, weapon_data_normalizer_effect_vfx);

  data_reg_struct_t(g_dataReg, AssetWeaponEffectSound);
  data_reg_field_t(g_dataReg, AssetWeaponEffectSound, asset, g_assetRefType);
  data_reg_field_t(g_dataReg, AssetWeaponEffectSound, delay, data_prim_t(TimeDuration));
  data_reg_field_t(g_dataReg, AssetWeaponEffectSound, duration, data_prim_t(TimeDuration));
  data_reg_field_t(g_dataReg, AssetWeaponEffectSound, originJoint, data_prim_t(StringHash), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectSound, gainMin, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectSound, gainMax, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectSound, pitchMin, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponEffectSound, pitchMax, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_normalizer_t(g_dataReg, AssetWeaponEffectSound, weapon_data_normalizer_effect_sound);

  data_reg_union_t(g_dataReg, AssetWeaponEffect, type);
  data_reg_choice_t(g_dataReg, AssetWeaponEffect, AssetWeaponEffect_Projectile, data_proj, t_AssetWeaponEffectProj);
  data_reg_choice_t(g_dataReg, AssetWeaponEffect, AssetWeaponEffect_Damage, data_dmg, t_AssetWeaponEffectDmg);
  data_reg_choice_t(g_dataReg, AssetWeaponEffect, AssetWeaponEffect_Animation, data_anim, t_AssetWeaponEffectAnim);
  data_reg_choice_t(g_dataReg, AssetWeaponEffect, AssetWeaponEffect_Vfx, data_vfx, t_AssetWeaponEffectVfx);
  data_reg_choice_t(g_dataReg, AssetWeaponEffect, AssetWeaponEffect_Sound, data_sound, t_AssetWeaponEffectSound);

  data_reg_struct_t(g_dataReg, AssetWeaponDef);
  data_reg_field_t(g_dataReg, AssetWeaponDef, name, data_prim_t(StringHash), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetWeaponDef, intervalMin, data_prim_t(TimeDuration));
  data_reg_field_t(g_dataReg, AssetWeaponDef, intervalMax, data_prim_t(TimeDuration));
  data_reg_field_t(g_dataReg, AssetWeaponDef, readySpeed, data_prim_t(f32));
  data_reg_field_t(g_dataReg, AssetWeaponDef, readyWhileMoving, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponDef, readyMinTime, data_prim_t(TimeDuration));
  data_reg_field_t(g_dataReg, AssetWeaponDef, readyAnim, data_prim_t(StringHash), .flags = DataFlags_NotEmpty | DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponDef, predictiveAim, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetWeaponDef, effects, t_AssetWeaponEffect, .container = DataContainer_HeapArray);

  data_reg_struct_t(g_dataReg, AssetWeaponMapDef);
  data_reg_field_t(g_dataReg, AssetWeaponMapDef, weapons, t_AssetWeaponDef, .container = DataContainer_HeapArray);
  // clang-format on

  g_assetWeaponDefMeta = data_meta_t(t_AssetWeaponMapDef);
}

void asset_load_weapons(
    EcsWorld*                 world,
    const AssetImportEnvComp* importEnv,
    const String              id,
    const EcsEntityId         entity,
    AssetSource*              src) {
  (void)importEnv;
  (void)id;

  AssetWeaponMapDef def;
  DataReadResult    result;
  if (src->format == AssetFormat_WeaponsBin) {
    data_read_bin(g_dataReg, src->data, g_allocHeap, g_assetWeaponDefMeta, mem_var(def), &result);
  } else {
    data_read_json(g_dataReg, src->data, g_allocHeap, g_assetWeaponDefMeta, mem_var(def), &result);
  }

  if (UNLIKELY(result.error)) {
    asset_mark_load_failure(world, entity, id, result.errorMsg, (i32)result.error);
    goto Ret;
  }

  if (src->format != AssetFormat_WeaponsBin) {
    // TODO: Instead of caching the definition it would be more optimal to cache the resulting map.
    asset_cache(world, entity, g_assetWeaponDefMeta, mem_var(def));
  }

  ecs_world_add_t(world, entity, AssetWeaponLoadComp, .def = def);

Ret:
  asset_repo_source_close(src);
}

u32 asset_weapon_refs(const AssetWeaponMapComp* map, EcsEntityId out[], const u32 outMax) {
  u32 outCount = 0;
  for (u32 i = 0; i != map->effects.count && outCount != outMax; ++i) {
    const AssetWeaponEffect* effect = &map->effects.values[i];
    switch (effect->type) {
    case AssetWeaponEffect_Projectile:
    case AssetWeaponEffect_Damage:
    case AssetWeaponEffect_Animation:
      break;
    case AssetWeaponEffect_Vfx:
      if (effect->data_vfx.asset.entity) {
        out[outCount++] = effect->data_vfx.asset.entity;
      }
      break;
    case AssetWeaponEffect_Sound:
      if (effect->data_sound.asset.entity) {
        out[outCount++] = effect->data_sound.asset.entity;
      }
      break;
    }
  }
  return outCount;
}

f32 asset_weapon_damage(const AssetWeaponMapComp* map, const AssetWeapon* weapon) {
  f32 damage = 0;
  for (u16 i = 0; i != weapon->effectCount; ++i) {
    const AssetWeaponEffect* effect = &map->effects.values[weapon->effectIndex + i];
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
    const AssetWeaponEffect* effect = &map->effects.values[weapon->effectIndex + i];
    switch (effect->type) {
    case AssetWeaponEffect_Damage:
      result |= effect->data_dmg.applyStatus;
      break;
    case AssetWeaponEffect_Projectile:
      result |= effect->data_proj.applyStatus;
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
      map->weapons.values,
      map->weapons.values + map->weapons.count,
      AssetWeapon,
      asset_weapon_compare,
      mem_struct(AssetWeapon, .name = nameHash).ptr);
}
