#include "asset_weapon.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_search.h"
#include "core_stringtable.h"
#include "core_thread.h"
#include "data.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "manager_internal.h"
#include "repo_internal.h"

static DataReg* g_dataReg;
static DataMeta g_dataMapDefMeta;

typedef struct {
  String name;
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
    data_reg_struct_t(g_dataReg, AssetWeaponDef);
    data_reg_field_t(g_dataReg, AssetWeaponDef, name, data_prim_t(String), .flags = DataFlags_NotEmpty);

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
  WeaponError_None            = 0,
  WeaponError_DuplicateWeapon = 1,

  WeaponError_Count,
} WeaponError;

static String weapon_error_str(const WeaponError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Multiple weapons with the same name"),
  };
  ASSERT(array_elems(g_msgs) == WeaponError_Count, "Incorrect number of error messages");
  return g_msgs[err];
}

static void asset_weaponmap_build(
    const AssetWeaponMapDef* def,
    DynArray*                outWeapons, // AssetWeapon[], needs to be already initialized.
    WeaponError*             err) {

  array_ptr_for_t(def->weapons, AssetWeaponDef, weaponDef) {
    const AssetWeapon weapon = {
        .nameHash = stringtable_add(g_stringtable, weaponDef->name),
    };
    if (dynarray_search_binary(outWeapons, asset_weapon_compare, &weapon)) {
      *err = WeaponError_DuplicateWeapon;
      return;
    }
    *dynarray_insert_sorted_t(outWeapons, AssetWeapon, asset_weapon_compare, &weapon) = weapon;
  }
  *err = WeaponError_None;
}

ecs_comp_define_public(AssetWeaponMapComp);

static void ecs_destruct_weaponmap_comp(void* data) {
  AssetWeaponMapComp* comp = data;
  if (comp->weapons) {
    alloc_free_array_t(g_alloc_heap, comp->weapons, comp->weaponCount);
  }
}

ecs_view_define(WeaponMapUnloadView) {
  ecs_access_with(AssetWeaponMapComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Remove any weapon-map asset component for unloaded assets.
 */
ecs_system_define(WeaponMapUnloadAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, WeaponMapUnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetWeaponMapComp);
  }
}

ecs_module_init(asset_weapon_module) {
  weapon_datareg_init();

  ecs_register_comp(AssetWeaponMapComp, .destructor = ecs_destruct_weaponmap_comp);

  ecs_register_view(WeaponMapUnloadView);

  ecs_register_system(WeaponMapUnloadAssetSys, ecs_view_id(WeaponMapUnloadView));
}

void asset_load_wea(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;

  DynArray weapons = dynarray_create_t(g_alloc_heap, AssetWeapon, 64);

  AssetWeaponMapDef def;
  String            errMsg;
  DataReadResult    readRes;
  data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataMapDefMeta, mem_var(def), &readRes);
  if (UNLIKELY(readRes.error)) {
    errMsg = readRes.errorMsg;
    goto Error;
  }

  WeaponError buildErr;
  asset_weaponmap_build(&def, &weapons, &buildErr);
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
      .weaponCount = weapons.size);

  ecs_world_add_empty_t(world, entity, AssetLoadedComp);
  goto Cleanup;

Error:
  log_e("Failed to load WeaponMap", log_param("error", fmt_text(errMsg)));
  ecs_world_add_empty_t(world, entity, AssetFailedComp);

Cleanup:
  asset_repo_source_close(src);
  dynarray_destroy(&weapons);
}

const AssetWeapon* asset_weapon_get(const AssetWeaponMapComp* map, const StringHash nameHash) {
  return search_binary_t(
      map->weapons,
      map->weapons + map->weaponCount,
      AssetWeapon,
      asset_weapon_compare,
      mem_struct(AssetWeapon, .nameHash = nameHash).ptr);
}
