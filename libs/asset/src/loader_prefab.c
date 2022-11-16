#include "asset_prefab.h"
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
  f32 speed;
} AssetPrefabTraitMovementDef;

typedef struct {
  AssetPrefabTraitType type;
  union {
    AssetPrefabTraitMovementDef data_movement;
  };
} AssetPrefabTraitDef;

typedef struct {
  String name;
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
    g_dataReg = data_reg_create(g_alloc_persist);

    // clang-format off
    data_reg_struct_t(g_dataReg, AssetPrefabTraitMovementDef);
    data_reg_field_t(g_dataReg, AssetPrefabTraitMovementDef, speed, data_prim_t(f32), .flags = DataFlags_NotEmpty);

    data_reg_union_t(g_dataReg, AssetPrefabTraitDef, type);
    data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Movement, data_movement, t_AssetPrefabTraitMovementDef);

    data_reg_struct_t(g_dataReg, AssetPrefabDef);
    data_reg_field_t(g_dataReg, AssetPrefabDef, name, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, AssetPrefabDef, traits, t_AssetPrefabTraitDef, .container = DataContainer_Array);

    data_reg_struct_t(g_dataReg, AssetPrefabMapDef);
    data_reg_field_t(g_dataReg, AssetPrefabMapDef, prefabs, t_AssetPrefabDef, .container = DataContainer_Array);
    // clang-format on

    g_dataMapDefMeta = data_meta_t(t_AssetPrefabMapDef);
  }
  thread_spinlock_unlock(&g_initLock);
}

static i8 prefab_compare(const void* a, const void* b) {
  return compare_stringhash(
      field_ptr(a, AssetPrefab, nameHash), field_ptr(b, AssetPrefab, nameHash));
}

typedef enum {
  PrefabError_None            = 0,
  PrefabError_DuplicatePrefab = 1,

  PrefabError_Count,
} PrefabError;

static String prefab_error_str(const PrefabError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Multiple prefabs with the same name"),
  };
  ASSERT(array_elems(g_msgs) == PrefabError_Count, "Incorrect number of error messages");
  return g_msgs[err];
}

typedef struct {
  EcsWorld*         world;
  AssetManagerComp* assetManager;
} BuildCtx;

static void prefab_trait_movement_build(
    BuildCtx*                          ctx,
    const AssetPrefabTraitMovementDef* def,
    AssetPrefabTraitMovement*          out,
    PrefabError*                       err) {
  (void)ctx;

  *out = (AssetPrefabTraitMovement){
      .speed = def->speed,
  };
  *err = PrefabError_None;
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
      .traitIndex = (u16)outTraits->size,
      .traitCount = (u16)def->traits.count,
  };

  array_ptr_for_t(def->traits, AssetPrefabTraitDef, traitDef) {
    AssetPrefabTrait* outTrait = dynarray_push_t(outTraits, AssetPrefabTrait);
    outTrait->type             = traitDef->type;

    switch (traitDef->type) {
    case AssetPrefabTrait_Movement:
      prefab_trait_movement_build(ctx, &traitDef->data_movement, &outTrait->data_movement, err);
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

ecs_comp_define_public(AssetPrefabMapComp);
ecs_comp_define(AssetPrefabLoadComp) { AssetSource* src; };

static void ecs_destruct_prefabmap_comp(void* data) {
  AssetPrefabMapComp* comp = data;
  if (comp->prefabs) {
    alloc_free_array_t(g_alloc_heap, comp->prefabs, comp->prefabCount);
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

    BuildCtx buildCtx = {
        .world        = world,
        .assetManager = manager,
    };

    PrefabError buildErr;
    prefabmap_build(&buildCtx, &def, &prefabs, &traits, &buildErr);
    data_destroy(g_dataReg, g_alloc_heap, g_dataMapDefMeta, mem_var(def));
    if (buildErr) {
      errMsg = prefab_error_str(buildErr);
      goto Error;
    }

    ecs_world_add_t(
        world,
        entity,
        AssetPrefabMapComp,
        .prefabs     = dynarray_copy_as_new(&prefabs, g_alloc_heap),
        .prefabCount = prefabs.size,
        .traits      = dynarray_copy_as_new(&traits, g_alloc_heap),
        .traitCount  = traits.size);

    ecs_world_add_empty_t(world, entity, AssetLoadedComp);
    goto Cleanup;

  Error:
    log_e("Failed to load PrefabMap", log_param("error", fmt_text(errMsg)));
    ecs_world_add_empty_t(world, entity, AssetFailedComp);

  Cleanup:
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
