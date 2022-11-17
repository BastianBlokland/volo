#include "asset_prefab.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_bitset.h"
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
  f32 x, y, z;
} PrefabVec3Def;

typedef struct {
  bool          navBlocker;
  PrefabVec3Def offset;
  f32           radius;
} AssetPrefabCollisionSphereDef;

typedef struct {
  bool          navBlocker;
  PrefabVec3Def offset;
  f32           radius, height;
} AssetPrefabCollisionCapsuleDef;

typedef struct {
  bool          navBlocker;
  PrefabVec3Def min, max;
} AssetPrefabCollisionBoxDef;

typedef struct {
  String graphicId;
} AssetPrefabTraitRenderableDef;

typedef struct {
  f32 scale;
} AssetPrefabTraitScaleDef;

typedef struct {
  f32 speed, radius;
} AssetPrefabTraitMovementDef;

typedef struct {
  f32 amount;
} AssetPrefabTraitHealthDef;

typedef struct {
  String weaponId;
} AssetPrefabTraitAttackDef;

typedef struct {
  AssetPrefabCollisionType type;
  union {
    AssetPrefabCollisionSphereDef  data_sphere;
    AssetPrefabCollisionCapsuleDef data_capsule;
    AssetPrefabCollisionBoxDef     data_box;
  };
} AssetPrefabTraitCollisionDef;

typedef struct {
  String behaviorId;
} AssetPrefabTraitBrainDef;

typedef struct {
  AssetPrefabTraitType type;
  union {
    AssetPrefabTraitRenderableDef data_renderable;
    AssetPrefabTraitScaleDef      data_scale;
    AssetPrefabTraitMovementDef   data_movement;
    AssetPrefabTraitHealthDef     data_health;
    AssetPrefabTraitAttackDef     data_attack;
    AssetPrefabTraitCollisionDef  data_collision;
    AssetPrefabTraitBrainDef      data_brain;
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
    data_reg_struct_t(g_dataReg, PrefabVec3Def);
    data_reg_field_t(g_dataReg, PrefabVec3Def, x, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, PrefabVec3Def, y, data_prim_t(f32), .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, PrefabVec3Def, z, data_prim_t(f32), .flags = DataFlags_Opt);

    data_reg_struct_t(g_dataReg, AssetPrefabCollisionSphereDef);
    data_reg_field_t(g_dataReg, AssetPrefabCollisionSphereDef, navBlocker, data_prim_t(bool));
    data_reg_field_t(g_dataReg, AssetPrefabCollisionSphereDef, offset, t_PrefabVec3Def, .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, AssetPrefabCollisionSphereDef, radius, data_prim_t(f32), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(g_dataReg, AssetPrefabCollisionCapsuleDef);
    data_reg_field_t(g_dataReg, AssetPrefabCollisionCapsuleDef, navBlocker, data_prim_t(bool));
    data_reg_field_t(g_dataReg, AssetPrefabCollisionCapsuleDef, offset, t_PrefabVec3Def, .flags = DataFlags_Opt);
    data_reg_field_t(g_dataReg, AssetPrefabCollisionCapsuleDef, radius, data_prim_t(f32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, AssetPrefabCollisionCapsuleDef, height, data_prim_t(f32), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(g_dataReg, AssetPrefabCollisionBoxDef);
    data_reg_field_t(g_dataReg, AssetPrefabCollisionBoxDef, navBlocker, data_prim_t(bool));
    data_reg_field_t(g_dataReg, AssetPrefabCollisionBoxDef, min, t_PrefabVec3Def);
    data_reg_field_t(g_dataReg, AssetPrefabCollisionBoxDef, max, t_PrefabVec3Def);

    data_reg_struct_t(g_dataReg, AssetPrefabTraitRenderableDef);
    data_reg_field_t(g_dataReg, AssetPrefabTraitRenderableDef, graphicId, data_prim_t(String), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(g_dataReg, AssetPrefabTraitScaleDef);
    data_reg_field_t(g_dataReg, AssetPrefabTraitScaleDef, scale, data_prim_t(f32), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(g_dataReg, AssetPrefabTraitMovementDef);
    data_reg_field_t(g_dataReg, AssetPrefabTraitMovementDef, speed, data_prim_t(f32), .flags = DataFlags_NotEmpty);
    data_reg_field_t(g_dataReg, AssetPrefabTraitMovementDef, radius, data_prim_t(f32), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(g_dataReg, AssetPrefabTraitHealthDef);
    data_reg_field_t(g_dataReg, AssetPrefabTraitHealthDef, amount, data_prim_t(f32), .flags = DataFlags_NotEmpty);

    data_reg_struct_t(g_dataReg, AssetPrefabTraitAttackDef);
    data_reg_field_t(g_dataReg, AssetPrefabTraitAttackDef, weaponId, data_prim_t(String), .flags = DataFlags_NotEmpty);

    data_reg_union_t(g_dataReg, AssetPrefabTraitCollisionDef, type);
    data_reg_choice_t(g_dataReg, AssetPrefabTraitCollisionDef, AssetPrefabCollision_Sphere, data_sphere, t_AssetPrefabCollisionSphereDef);
    data_reg_choice_t(g_dataReg, AssetPrefabTraitCollisionDef, AssetPrefabCollision_Capsule, data_capsule, t_AssetPrefabCollisionCapsuleDef);
    data_reg_choice_t(g_dataReg, AssetPrefabTraitCollisionDef, AssetPrefabCollision_Box, data_box, t_AssetPrefabCollisionBoxDef);

    data_reg_struct_t(g_dataReg, AssetPrefabTraitBrainDef);
    data_reg_field_t(g_dataReg, AssetPrefabTraitBrainDef, behaviorId, data_prim_t(String), .flags = DataFlags_NotEmpty);

    data_reg_union_t(g_dataReg, AssetPrefabTraitDef, type);
    data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Renderable, data_renderable, t_AssetPrefabTraitRenderableDef);
    data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Scale, data_scale, t_AssetPrefabTraitScaleDef);
    data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Movement, data_movement, t_AssetPrefabTraitMovementDef);
    data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Health, data_health, t_AssetPrefabTraitHealthDef);
    data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Attack, data_attack, t_AssetPrefabTraitAttackDef);
    data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Collision, data_collision, t_AssetPrefabTraitCollisionDef);
    data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Brain, data_brain, t_AssetPrefabTraitBrainDef);

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
  PrefabError_DuplicateTrait  = 2,

  PrefabError_Count,
} PrefabError;

static String prefab_error_str(const PrefabError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Multiple prefabs with the same name"),
      string_static("Prefab defines the same trait more then once"),
  };
  ASSERT(array_elems(g_msgs) == PrefabError_Count, "Incorrect number of error messages");
  return g_msgs[err];
}

typedef struct {
  EcsWorld*         world;
  AssetManagerComp* assetManager;
} BuildCtx;

static GeoVector prefab_build_vec3(const PrefabVec3Def* def) {
  return geo_vector(def->x, def->y, def->z);
}

static void prefab_build_trait_collision(
    const AssetPrefabTraitCollisionDef* def, AssetPrefabTraitCollision* out) {

  switch (def->type) {
  case AssetPrefabCollision_Sphere:
    *out = (AssetPrefabTraitCollision){
        .type               = AssetPrefabCollision_Sphere,
        .navBlocker         = def->data_sphere.navBlocker,
        .data_sphere.offset = prefab_build_vec3(&def->data_sphere.offset),
        .data_sphere.radius = def->data_sphere.radius,
    };
    break;
  case AssetPrefabCollision_Capsule:
    *out = (AssetPrefabTraitCollision){
        .type                = AssetPrefabCollision_Capsule,
        .navBlocker          = def->data_capsule.navBlocker,
        .data_capsule.offset = prefab_build_vec3(&def->data_capsule.offset),
        .data_capsule.radius = def->data_capsule.radius,
        .data_capsule.height = def->data_capsule.height,
    };
    break;
  case AssetPrefabCollision_Box:
    *out = (AssetPrefabTraitCollision){
        .type         = AssetPrefabCollision_Box,
        .navBlocker   = def->data_box.navBlocker,
        .data_box.min = prefab_build_vec3(&def->data_box.min),
        .data_box.max = prefab_build_vec3(&def->data_box.max),
    };
    break;
  }
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
    case AssetPrefabTrait_Scale:
      outTrait->data_scale = (AssetPrefabTraitScale){
          .scale = traitDef->data_scale.scale,
      };
      break;
    case AssetPrefabTrait_Movement:
      outTrait->data_movement = (AssetPrefabTraitMovement){
          .speed  = traitDef->data_movement.speed,
          .radius = traitDef->data_movement.radius,
      };
      break;
    case AssetPrefabTrait_Health:
      outTrait->data_health = (AssetPrefabTraitHealth){
          .amount = traitDef->data_health.amount,
      };
      break;
    case AssetPrefabTrait_Attack:
      outTrait->data_attack = (AssetPrefabTraitAttack){
          .weapon = string_hash(traitDef->data_attack.weaponId),
      };
      break;
    case AssetPrefabTrait_Collision:
      prefab_build_trait_collision(&traitDef->data_collision, &outTrait->data_collision);
      break;
    case AssetPrefabTrait_Brain:
      outTrait->data_brain = (AssetPrefabTraitBrain){
          .behavior = asset_lookup(ctx->world, manager, traitDef->data_brain.behaviorId),
      };
      break;
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
