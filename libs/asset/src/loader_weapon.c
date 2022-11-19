#include "asset_weapon.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
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
  String originJoint;
  f32    delay, lifetime;
  f32    spreadAngle;
  f32    speed;
  f32    damage;
  f32    impactLifetime;
  String vfxIdProjectile, vfxIdImpact;
} AssetWeaponEffectProjDef;

typedef struct {
  String originJoint;
  f32    delay;
  f32    radius;
  f32    damage;
} AssetWeaponEffectDmgDef;

typedef struct {
  String layer;
  f32    delay;
  f32    speed;
} AssetWeaponEffectAnimDef;

typedef struct {
  String originJoint;
  f32    delay, duration;
  String assetId;
} AssetWeaponEffectVfxDef;

typedef struct {
  AssetWeaponEffectType type;
  union {
    AssetWeaponEffectProjDef data_proj;
    AssetWeaponEffectDmgDef  data_dmg;
    AssetWeaponEffectAnimDef data_anim;
    AssetWeaponEffectVfxDef  data_vfx;
  };
} AssetWeaponEffectDef;

typedef struct {
  String name;
  f32    intervalMin, intervalMax;
  f32    aimSpeed;
  f32    aimMinTime;
  String aimAnim;
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

static void weapon_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    g_dataReg = data_reg_create(g_alloc_persist);

    // clang-format off
    data_reg_struct_t(g_dataReg, AssetWeaponEffectProjDef);
    data_reg_field_t(g_dataReg, AssetWeaponEffectProjDef, originJoint, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, AssetWeaponEffectProjDef, delay, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetWeaponEffectProjDef, lifetime, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetWeaponEffectProjDef, spreadAngle, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetWeaponEffectProjDef, speed, data_prim_t(f32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, AssetWeaponEffectProjDef, damage, data_prim_t(f32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, AssetWeaponEffectProjDef, impactLifetime, data_prim_t(f32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, AssetWeaponEffectProjDef, vfxIdProjectile, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, AssetWeaponEffectProjDef, vfxIdImpact, data_prim_t(String), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(g_dataReg, AssetWeaponEffectDmgDef);
    data_reg_field_t(g_dataReg, AssetWeaponEffectDmgDef, originJoint, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, AssetWeaponEffectDmgDef, delay, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetWeaponEffectDmgDef, damage, data_prim_t(f32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, AssetWeaponEffectDmgDef, radius, data_prim_t(f32), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(g_dataReg, AssetWeaponEffectVfxDef);
    data_reg_field_t(g_dataReg, AssetWeaponEffectVfxDef, assetId, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, AssetWeaponEffectVfxDef, delay, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetWeaponEffectVfxDef, duration, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetWeaponEffectVfxDef, originJoint, data_prim_t(String), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(g_dataReg, AssetWeaponEffectAnimDef);
    data_reg_field_t(g_dataReg, AssetWeaponEffectAnimDef, layer, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, AssetWeaponEffectAnimDef, delay, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetWeaponEffectAnimDef, speed, data_prim_t(f32), .flags = DataFlags_NotEmpty);

    data_reg_union_t(g_dataReg, AssetWeaponEffectDef, type);
    data_reg_choice_t(g_dataReg, AssetWeaponEffectDef, AssetWeaponEffect_Projectile, data_proj, t_AssetWeaponEffectProjDef);
    data_reg_choice_t(g_dataReg, AssetWeaponEffectDef, AssetWeaponEffect_Damage, data_dmg, t_AssetWeaponEffectDmgDef);
    data_reg_choice_t(g_dataReg, AssetWeaponEffectDef, AssetWeaponEffect_Animation, data_anim, t_AssetWeaponEffectAnimDef);
    data_reg_choice_t(g_dataReg, AssetWeaponEffectDef, AssetWeaponEffect_Vfx, data_vfx, t_AssetWeaponEffectVfxDef);

    data_reg_struct_t(g_dataReg, AssetWeaponDef);
    data_reg_field_t(g_dataReg, AssetWeaponDef, name, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, AssetWeaponDef, intervalMin, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetWeaponDef, intervalMax, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetWeaponDef, aimSpeed, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetWeaponDef, aimMinTime, data_prim_t(f32));
    data_reg_field_t(g_dataReg, AssetWeaponDef, aimAnim, data_prim_t(String), .flags = DataFlags_NotEmpty | DataFlags_Opt);
    data_reg_field_t(g_dataReg, AssetWeaponDef, effects, t_AssetWeaponEffectDef, .container = DataContainer_Array);

    data_reg_struct_t(g_dataReg, AssetWeaponMapDef);
    data_reg_field_t(g_dataReg, AssetWeaponMapDef, weapons, t_AssetWeaponDef, .container = DataContainer_Array);
    // clang-format on

    g_dataMapDefMeta = data_meta_t(t_AssetWeaponMapDef);
  }
  thread_spinlock_unlock(&g_initLock);
}

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

static void weapon_effect_proj_build(
    BuildCtx*                       ctx,
    const AssetWeaponEffectProjDef* def,
    AssetWeaponEffectProj*          out,
    WeaponError*                    err) {
  *out = (AssetWeaponEffectProj){
      .originJoint    = string_hash(def->originJoint),
      .delay          = (TimeDuration)time_seconds(def->delay),
      .lifetime       = (TimeDuration)time_seconds(def->lifetime),
      .spreadAngle    = def->spreadAngle,
      .speed          = def->speed,
      .damage         = def->damage,
      .impactLifetime = (TimeDuration)time_seconds(def->impactLifetime),
      .vfxProjectile  = asset_lookup(ctx->world, ctx->assetManager, def->vfxIdProjectile),
      .vfxImpact      = asset_lookup(ctx->world, ctx->assetManager, def->vfxIdImpact),
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
      .originJoint = string_hash(def->originJoint),
      .delay       = (TimeDuration)time_seconds(def->delay),
      .damage      = def->damage,
      .radius      = def->radius,
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
      .layer = string_hash(def->layer),
      .delay = (TimeDuration)time_seconds(def->delay),
      .speed = def->speed,
  };
  *err = WeaponError_None;
}

static void weapon_effect_vfx_build(
    BuildCtx*                      ctx,
    const AssetWeaponEffectVfxDef* def,
    AssetWeaponEffectVfx*          out,
    WeaponError*                   err) {
  *out = (AssetWeaponEffectVfx){
      .originJoint = string_hash(def->originJoint),
      .delay       = (TimeDuration)time_seconds(def->delay),
      .duration    = (TimeDuration)time_seconds(def->duration),
      .asset       = asset_lookup(ctx->world, ctx->assetManager, def->assetId),
  };
  *err = WeaponError_None;
}

static void weapon_build(
    BuildCtx*             ctx,
    const AssetWeaponDef* def,
    DynArray*             outEffects, // AssetWeaponEffect[], needs to be already initialized.
    AssetWeapon*          outWeapon,
    WeaponError*          err) {

  *err       = WeaponError_None;
  *outWeapon = (AssetWeapon){
      .nameHash    = stringtable_add(g_stringtable, def->name),
      .intervalMin = (TimeDuration)time_seconds(def->intervalMin),
      .intervalMax = (TimeDuration)time_seconds(def->intervalMax),
      .aimSpeed    = def->aimSpeed,
      .aimMinTime  = (TimeDuration)time_seconds(def->aimMinTime),
      .aimAnim     = string_is_empty(def->aimAnim) ? 0 : string_hash(def->aimAnim),
      .effectIndex = (u16)outEffects->size,
      .effectCount = (u16)def->effects.count,
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
    alloc_free_array_t(g_alloc_heap, comp->weapons, comp->weaponCount);
  }
  if (comp->effects) {
    alloc_free_array_t(g_alloc_heap, comp->effects, comp->effectCount);
  }
}

static void ecs_destruct_weapon_load_comp(void* data) {
  AssetWeaponLoadComp* comp = data;
  asset_repo_source_close(comp->src);
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(LoadView) { ecs_access_read(AssetWeaponLoadComp); }

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
    const AssetSource* src    = ecs_view_read_t(itr, AssetWeaponLoadComp)->src;

    DynArray weapons = dynarray_create_t(g_alloc_heap, AssetWeapon, 64);
    DynArray effects = dynarray_create_t(g_alloc_heap, AssetWeaponEffect, 64);

    AssetWeaponMapDef def;
    String            errMsg;
    DataReadResult    readRes;
    data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataMapDefMeta, mem_var(def), &readRes);
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
    data_destroy(g_dataReg, g_alloc_heap, g_dataMapDefMeta, mem_var(def));
    if (buildErr) {
      errMsg = weapon_error_str(buildErr);
      goto Error;
    }

    ecs_world_add_t(
        world,
        entity,
        AssetWeaponMapComp,
        .weapons     = dynarray_copy_as_new(&weapons, g_alloc_heap),
        .weaponCount = weapons.size,
        .effects     = dynarray_copy_as_new(&effects, g_alloc_heap),
        .effectCount = effects.size);

    ecs_world_add_empty_t(world, entity, AssetLoadedComp);
    goto Cleanup;

  Error:
    log_e("Failed to load WeaponMap", log_param("error", fmt_text(errMsg)));
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
  weapon_datareg_init();

  ecs_register_comp(AssetWeaponMapComp, .destructor = ecs_destruct_weaponmap_comp);
  ecs_register_comp(AssetWeaponLoadComp, .destructor = ecs_destruct_weapon_load_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(UnloadView);

  ecs_register_system(LoadWeaponAssetSys, ecs_view_id(ManagerView), ecs_view_id(LoadView));
  ecs_register_system(UnloadWeaponAssetSys, ecs_view_id(UnloadView));
}

void asset_load_wea(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;
  ecs_world_add_t(world, entity, AssetWeaponLoadComp, .src = src);
}

const AssetWeapon* asset_weapon_get(const AssetWeaponMapComp* map, const StringHash nameHash) {
  return search_binary_t(
      map->weapons,
      map->weapons + map->weaponCount,
      AssetWeapon,
      asset_weapon_compare,
      mem_struct(AssetWeapon, .nameHash = nameHash).ptr);
}
