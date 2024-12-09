#include "asset_prefab.h"
#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_dynarray.h"
#include "core_float.h"
#include "core_math.h"
#include "core_search.h"
#include "core_stringtable.h"
#include "data_read.h"
#include "data_utils.h"
#include "ecs_entity.h"
#include "ecs_utils.h"
#include "ecs_view.h"
#include "log_logger.h"

#include "data_internal.h"
#include "manager_internal.h"
#include "repo_internal.h"

#define trait_movement_weight_min 0.1f

DataMeta g_assetPrefabDefMeta;

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

static void prefab_set_flags_init(void) {
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
  String assetId;
  bool   persistent;
} AssetPrefabValueSoundDef;

typedef struct {
  String               name;
  AssetPrefabValueType type;
  union {
    f64                      data_number;
    bool                     data_bool;
    GeoVector                data_vector3;
    GeoColor                 data_color;
    String                   data_string;
    String                   data_asset;
    AssetPrefabValueSoundDef data_sound;
  };
} AssetPrefabValueDef;

typedef struct {
  HeapArray_t(String) assetIds;
  f32  gainMin, gainMax;
  f32  pitchMin, pitchMax;
  bool looping;
  bool persistent;
} AssetPrefabTraitSoundDef;

typedef struct {
  bool             navBlocker;
  AssetPrefabShape shape;
} AssetPrefabTraitCollisionDef;

typedef struct {
  HeapArray_t(String) scriptIds;
  HeapArray_t(AssetPrefabValue) knowledge;
} AssetPrefabTraitScriptDef;

typedef struct {
  AssetPrefabTraitType type;
  union {
    AssetPrefabTraitName         data_name;
    AssetPrefabTraitSetMember    data_setMember;
    AssetPrefabTraitRenderable   data_renderable;
    AssetPrefabTraitVfx          data_vfx;
    AssetPrefabTraitDecal        data_decal;
    AssetPrefabTraitSoundDef     data_sound;
    AssetPrefabTraitLightPoint   data_lightPoint;
    AssetPrefabTraitLightDir     data_lightDir;
    AssetPrefabTraitLightAmbient data_lightAmbient;
    AssetPrefabTraitLifetime     data_lifetime;
    AssetPrefabTraitMovement     data_movement;
    AssetPrefabTraitFootstep     data_footstep;
    AssetPrefabTraitHealth       data_health;
    AssetPrefabTraitAttack       data_attack;
    AssetPrefabTraitCollisionDef data_collision;
    AssetPrefabTraitScriptDef    data_script;
    AssetPrefabTraitBark         data_bark;
    AssetPrefabTraitLocation     data_location;
    AssetPrefabTraitStatus       data_status;
    AssetPrefabTraitVision       data_vision;
    AssetPrefabTraitAttachment   data_attachment;
    AssetPrefabTraitProduction   data_production;
  };
} AssetPrefabTraitDef;

typedef struct {
  String name;
  bool   isVolatile;
  HeapArray_t(AssetPrefabTraitDef) traits;
} AssetPrefabDef;

typedef struct {
  HeapArray_t(AssetPrefabDef) prefabs;
} AssetPrefabMapDef;

static i8 prefab_compare(const void* a, const void* b) {
  return compare_stringhash(
      field_ptr(a, AssetPrefab, nameHash), field_ptr(b, AssetPrefab, nameHash));
}

typedef enum {
  PrefabError_None,
  PrefabError_DuplicatePrefab,
  PrefabError_DuplicateTrait,
  PrefabError_PrefabCountExceedsMax,
  PrefabError_SoundAssetCountExceedsMax,
  PrefabError_ScriptAssetCountExceedsMax,

  PrefabError_Count,
} PrefabError;

static String prefab_error_str(const PrefabError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Multiple prefabs with the same name"),
      string_static("Prefab defines the same trait more then once"),
      string_static("Prefab count exceeds the maximum"),
      string_static("Sound asset count exceeds the maximum"),
      string_static("Script asset count exceeds the maximum"),
  };
  ASSERT(array_elems(g_msgs) == PrefabError_Count, "Incorrect number of error messages");
  return g_msgs[err];
}

typedef struct {
  EcsWorld*         world;
  AssetManagerComp* assetManager;
} BuildCtx;

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
    res.data_vector3 = def->data_vector3;
    break;
  case AssetPrefabValue_Color:
    res.type       = AssetPrefabValue_Color;
    res.data_color = def->data_color;
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
  heap_array_for_t(def->traits, AssetPrefabTraitDef, traitDef) {
    if (bitset_test(addedTraits, traitDef->type)) {
      *err = PrefabError_DuplicateTrait;
      return;
    }
    bitset_set(addedTraits, traitDef->type);

    AssetPrefabTrait* outTrait = dynarray_push_t(outTraits, AssetPrefabTrait);
    outTrait->type             = traitDef->type;

    switch (traitDef->type) {
    case AssetPrefabTrait_Name:
      outTrait->data_name = traitDef->data_name;
      break;
    case AssetPrefabTrait_SetMember:
      outTrait->data_setMember = traitDef->data_setMember;
      for (u32 i = 0; i != asset_prefab_sets_max; ++i) {
        const StringHash set = outTrait->data_setMember.sets[i];
        if (set) {
          outPrefab->flags |= prefab_set_flags(set);
        }
      }
      break;
    case AssetPrefabTrait_Renderable:
      outTrait->data_renderable = traitDef->data_renderable;
      break;
    case AssetPrefabTrait_Vfx:
      outTrait->data_vfx = traitDef->data_vfx;
      break;
    case AssetPrefabTrait_Decal:
      outTrait->data_decal = traitDef->data_decal;
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
      outTrait->data_lightPoint = traitDef->data_lightPoint;
      break;
    case AssetPrefabTrait_LightDir:
      outTrait->data_lightDir = traitDef->data_lightDir;
      break;
    case AssetPrefabTrait_LightAmbient:
      outTrait->data_lightAmbient = traitDef->data_lightAmbient;
      break;
    case AssetPrefabTrait_Lifetime:
      outTrait->data_lifetime = traitDef->data_lifetime;
      break;
    case AssetPrefabTrait_Movement:
      outTrait->data_movement = traitDef->data_movement;
      break;
    case AssetPrefabTrait_Footstep:
      outTrait->data_footstep = traitDef->data_footstep;
      break;
    case AssetPrefabTrait_Health:
      outTrait->data_health = traitDef->data_health;
      break;
    case AssetPrefabTrait_Attack:
      outTrait->data_attack = traitDef->data_attack;
      break;
    case AssetPrefabTrait_Collision:
      outTrait->data_collision = (AssetPrefabTraitCollision){
          .navBlocker = traitDef->data_collision.navBlocker,
          .shape      = traitDef->data_collision.shape,
      };
      break;
    case AssetPrefabTrait_Script: {
      const AssetPrefabTraitScriptDef* scriptDef   = &traitDef->data_script;
      const u32                        scriptCount = (u32)scriptDef->scriptIds.count;
      if (UNLIKELY(scriptCount > asset_prefab_scripts_max)) {
        *err = PrefabError_ScriptAssetCountExceedsMax;
        return;
      }
      outTrait->data_script = (AssetPrefabTraitScript){
          .scriptAssetCount = (u8)scriptCount,
          .knowledgeIndex   = (u16)outValues->size,
          .knowledgeCount   = (u16)scriptDef->knowledge.count,
      };
      for (u32 i = 0; i != scriptCount; ++i) {
        const String assetId                  = scriptDef->scriptIds.values[i];
        outTrait->data_script.scriptAssets[i] = asset_lookup(ctx->world, manager, assetId);
      }
      heap_array_for_t(scriptDef->knowledge, AssetPrefabValueDef, valDef) {
        *dynarray_push_t(outValues, AssetPrefabValue) = prefab_build_value(ctx, valDef);
      }
    } break;
    case AssetPrefabTrait_Bark:
      outTrait->data_bark = traitDef->data_bark;
      break;
    case AssetPrefabTrait_Location:
      outTrait->data_location = traitDef->data_location;
      break;
    case AssetPrefabTrait_Status:
      outTrait->data_status = traitDef->data_status;
      break;
    case AssetPrefabTrait_Vision:
      outTrait->data_vision = traitDef->data_vision;
      break;
    case AssetPrefabTrait_Attachment:
      outTrait->data_attachment = traitDef->data_attachment;
      break;
    case AssetPrefabTrait_Production:
      outTrait->data_production = traitDef->data_production;
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
    DynArray*                outValues,  // AssetPrefabValue[], needs to be already initialized.
    PrefabError*             err) {

  heap_array_for_t(def->prefabs, AssetPrefabDef, prefabDef) {
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
    alloc_free_array_t(g_allocHeap, comp->prefabs, comp->prefabCount);
    alloc_free_array_t(g_allocHeap, comp->userIndexLookup, comp->prefabCount);
  }
  if (comp->traits.values) {
    alloc_free_array_t(g_allocHeap, comp->traits.values, comp->traits.count);
  }
  if (comp->values.values) {
    alloc_free_array_t(g_allocHeap, comp->values.values, comp->values.count);
  }
}

static void ecs_destruct_prefab_load_comp(void* data) {
  AssetPrefabLoadComp* comp = data;
  asset_repo_source_close(comp->src);
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }

ecs_view_define(LoadView) {
  ecs_access_read(AssetComp);
  ecs_access_read(AssetPrefabLoadComp);
}

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
    const String       id     = asset_id(ecs_view_read_t(itr, AssetComp));
    const AssetSource* src    = ecs_view_read_t(itr, AssetPrefabLoadComp)->src;

    DynArray prefabs = dynarray_create_t(g_allocHeap, AssetPrefab, 64);
    DynArray traits  = dynarray_create_t(g_allocHeap, AssetPrefabTrait, 64);
    DynArray values  = dynarray_create_t(g_allocHeap, AssetPrefabValue, 32);

    AssetPrefabMapDef def;
    String            errMsg;
    DataReadResult    readRes;
    data_read_json(g_dataReg, src->data, g_allocHeap, g_assetPrefabDefMeta, mem_var(def), &readRes);
    if (UNLIKELY(readRes.error)) {
      errMsg = readRes.errorMsg;
      goto Error;
    }
    if (UNLIKELY(def.prefabs.count > u16_max)) {
      errMsg = prefab_error_str(PrefabError_PrefabCountExceedsMax);
      goto Error;
    }

    // Resolve asset references.
    asset_data_patch_refs(world, manager, g_assetPrefabDefMeta, mem_var(def));

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

    u16* userIndexLookup = prefabs.size ? alloc_array_t(g_allocHeap, u16, prefabs.size) : null;
    prefabmap_build_user_index_lookup(
        &def, dynarray_begin_t(&prefabs, AssetPrefab), userIndexLookup);

    ecs_world_add_t(
        world,
        entity,
        AssetPrefabMapComp,
        .prefabs         = dynarray_copy_as_new(&prefabs, g_allocHeap),
        .userIndexLookup = userIndexLookup,
        .prefabCount     = prefabs.size,
        .traits.values   = dynarray_copy_as_new(&traits, g_allocHeap),
        .traits.count    = traits.size,
        .values.values   = dynarray_copy_as_new(&values, g_allocHeap),
        .values.count    = values.size);

    ecs_world_add_empty_t(world, entity, AssetLoadedComp);
    goto Cleanup;

  Error:
    log_e(
        "Failed to load PrefabMap",
        log_param("id", fmt_text(id)),
        log_param("entity", ecs_entity_fmt(entity)),
        log_param("error", fmt_text(errMsg)));
    ecs_world_add_empty_t(world, entity, AssetFailedComp);

  Cleanup:
    data_destroy(g_dataReg, g_allocHeap, g_assetPrefabDefMeta, mem_var(def));
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
  ecs_register_comp(AssetPrefabMapComp, .destructor = ecs_destruct_prefabmap_comp);
  ecs_register_comp(AssetPrefabLoadComp, .destructor = ecs_destruct_prefab_load_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(UnloadView);

  ecs_register_system(LoadPrefabAssetSys, ecs_view_id(ManagerView), ecs_view_id(LoadView));
  ecs_register_system(UnloadPrefabAssetSys, ecs_view_id(UnloadView));
}

static bool prefab_data_normalizer_light_point(const Mem data) {
  AssetPrefabTraitLightPoint* light = mem_as_t(data, AssetPrefabTraitLightPoint);
  light->radius                     = math_max(0.01f, light->radius);
  return true;
}

static bool prefab_data_normalizer_attachment(const Mem data) {
  AssetPrefabTraitAttachment* attach = mem_as_t(data, AssetPrefabTraitAttachment);
  attach->attachmentScale = attach->attachmentScale < f32_epsilon ? 1.0f : attach->attachmentScale;
  return true;
}

static bool prefab_data_normalizer_movement(const Mem data) {
  AssetPrefabTraitMovement* movement = mem_as_t(data, AssetPrefabTraitMovement);
  movement->weight                   = math_max(movement->weight, trait_movement_weight_min);
  return true;
}

static bool prefab_data_normalizer_production(const Mem data) {
  AssetPrefabTraitProduction* prod = mem_as_t(data, AssetPrefabTraitProduction);
  prod->rallySoundGain             = prod->rallySoundGain <= 0.0f ? 1.0f : prod->rallySoundGain;
  return true;
}

void asset_data_init_prefab(void) {
  prefab_set_flags_init();

  // clang-format off
  /**
    * Status indices correspond to the 'SceneStatusType' values as defined in 'scene_status.h'.
    * NOTE: Unfortunately we cannot reference the SceneStatusType enum directly as that would
    * require an undesired dependency on the scene library.
    * NOTE: This is a virtual data type, meaning there is no matching AssetPrefabStatusMask C type.
    */
  data_reg_enum_multi_t(g_dataReg, AssetPrefabStatusMask);
  data_reg_const_custom(g_dataReg, AssetPrefabStatusMask, Burning,  1 << 0);
  data_reg_const_custom(g_dataReg, AssetPrefabStatusMask, Bleeding, 1 << 1);
  data_reg_const_custom(g_dataReg, AssetPrefabStatusMask, Healing,  1 << 2);
  data_reg_const_custom(g_dataReg, AssetPrefabStatusMask, Veteran,  1 << 3);

  /**
    * NOTE: This is a virtual data type, meaning there is no matching AssetPrefabNavLayer C type.
    */
  data_reg_enum_t(g_dataReg, AssetPrefabNavLayer);
  data_reg_const_custom(g_dataReg, AssetPrefabNavLayer, Normal,  0);
  data_reg_const_custom(g_dataReg, AssetPrefabNavLayer, Large, 1);

  data_reg_union_t(g_dataReg, AssetPrefabShape, type);
  data_reg_choice_t(g_dataReg, AssetPrefabShape, AssetPrefabShape_Sphere, data_sphere, g_assetGeoSphereType);
  data_reg_choice_t(g_dataReg, AssetPrefabShape, AssetPrefabShape_Capsule, data_capsule, g_assetGeoCapsuleType);
  data_reg_choice_t(g_dataReg, AssetPrefabShape, AssetPrefabShape_Box, data_box, g_assetGeoBoxRotatedType);

  data_reg_struct_t(g_dataReg, AssetPrefabValueSoundDef);
  data_reg_field_t(g_dataReg, AssetPrefabValueSoundDef, assetId, data_prim_t(String), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabValueSoundDef, persistent, data_prim_t(bool), .flags = DataFlags_Opt);

  data_reg_union_t(g_dataReg, AssetPrefabValueDef, type);
  data_reg_union_name_t(g_dataReg, AssetPrefabValueDef, name);
  data_reg_choice_t(g_dataReg, AssetPrefabValueDef, AssetPrefabValue_Number, data_number, data_prim_t(f64));
  data_reg_choice_t(g_dataReg, AssetPrefabValueDef, AssetPrefabValue_Bool, data_bool, data_prim_t(bool));
  data_reg_choice_t(g_dataReg, AssetPrefabValueDef, AssetPrefabValue_Vector3, data_vector3, g_assetGeoVec3Type);
  data_reg_choice_t(g_dataReg, AssetPrefabValueDef, AssetPrefabValue_Color, data_color, g_assetGeoColorType);
  data_reg_choice_t(g_dataReg, AssetPrefabValueDef, AssetPrefabValue_String, data_string, data_prim_t(String), .flags = DataFlags_Intern);
  data_reg_choice_t(g_dataReg, AssetPrefabValueDef, AssetPrefabValue_Asset, data_asset, data_prim_t(String));
  data_reg_choice_t(g_dataReg, AssetPrefabValueDef, AssetPrefabValue_Sound, data_sound, t_AssetPrefabValueSoundDef);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitName);
  data_reg_field_t(g_dataReg, AssetPrefabTraitName, name, data_prim_t(StringHash), .flags = DataFlags_NotEmpty);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitSetMember);
  data_reg_field_t(g_dataReg, AssetPrefabTraitSetMember, sets, data_prim_t(StringHash), .container = DataContainer_InlineArray, .fixedCount = asset_prefab_sets_max, .flags = DataFlags_NotEmpty);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitRenderable);
  data_reg_field_t(g_dataReg, AssetPrefabTraitRenderable, graphic, g_assetRefType);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitVfx);
  data_reg_field_t(g_dataReg, AssetPrefabTraitVfx, asset, g_assetRefType);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitDecal);
  data_reg_field_t(g_dataReg, AssetPrefabTraitDecal, asset, g_assetRefType);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitSoundDef);
  data_reg_field_t(g_dataReg, AssetPrefabTraitSoundDef, assetIds, data_prim_t(String), .container = DataContainer_HeapArray, .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitSoundDef, gainMin, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitSoundDef, gainMax, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitSoundDef, pitchMin, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitSoundDef, pitchMax, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitSoundDef, looping, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetPrefabTraitSoundDef, persistent, data_prim_t(bool), .flags = DataFlags_Opt);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitLightPoint);
  data_reg_field_t(g_dataReg, AssetPrefabTraitLightPoint, radiance, g_assetGeoColorType, .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitLightPoint, radius, data_prim_t(f32), .flags = DataFlags_NotEmpty);
  data_reg_normalizer_t(g_dataReg, AssetPrefabTraitLightPoint, prefab_data_normalizer_light_point);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitLightDir);
  data_reg_field_t(g_dataReg, AssetPrefabTraitLightDir, radiance, g_assetGeoColorType, .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitLightDir, shadows, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetPrefabTraitLightDir, coverage, data_prim_t(bool), .flags = DataFlags_Opt);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitLightAmbient);
  data_reg_field_t(g_dataReg, AssetPrefabTraitLightAmbient, intensity, data_prim_t(f32), .flags = DataFlags_NotEmpty);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitLifetime);
  data_reg_field_t(g_dataReg, AssetPrefabTraitLifetime, duration, data_prim_t(TimeDuration), .flags = DataFlags_NotEmpty);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitMovement);
  data_reg_field_t(g_dataReg, AssetPrefabTraitMovement, speed, data_prim_t(f32), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitMovement, rotationSpeed, data_prim_t(Angle), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitMovement, radius, data_prim_t(f32), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitMovement, weight, data_prim_t(f32), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitMovement, moveAnimation, data_prim_t(StringHash), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetPrefabTraitMovement, navLayer, t_AssetPrefabNavLayer, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetPrefabTraitMovement, wheeled, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetPrefabTraitMovement, wheeledAcceleration, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_normalizer_t(g_dataReg, AssetPrefabTraitMovement, prefab_data_normalizer_movement);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitFootstep);
  data_reg_field_t(g_dataReg, AssetPrefabTraitFootstep, jointA, data_prim_t(StringHash), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitFootstep, jointB, data_prim_t(StringHash), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitFootstep, decalA, g_assetRefType);
  data_reg_field_t(g_dataReg, AssetPrefabTraitFootstep, decalB, g_assetRefType);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitHealth);
  data_reg_field_t(g_dataReg, AssetPrefabTraitHealth, amount, data_prim_t(f32), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitHealth, deathDestroyDelay, data_prim_t(TimeDuration));
  data_reg_field_t(g_dataReg, AssetPrefabTraitHealth, deathEffectPrefab, data_prim_t(StringHash), .flags = DataFlags_Opt | DataFlags_NotEmpty);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitAttack);
  data_reg_field_t(g_dataReg, AssetPrefabTraitAttack, weapon, data_prim_t(StringHash), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitAttack, aimJoint, data_prim_t(StringHash), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitAttack, aimSpeed, data_prim_t(Angle), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitAttack, targetRangeMin, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetPrefabTraitAttack, targetRangeMax, data_prim_t(f32), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitAttack, targetExcludeUnreachable, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetPrefabTraitAttack, targetExcludeObscured, data_prim_t(bool), .flags = DataFlags_Opt);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitCollisionDef);
  data_reg_field_t(g_dataReg, AssetPrefabTraitCollisionDef, navBlocker, data_prim_t(bool));
  data_reg_field_t(g_dataReg, AssetPrefabTraitCollisionDef, shape, t_AssetPrefabShape);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitScriptDef);
  data_reg_field_t(g_dataReg, AssetPrefabTraitScriptDef, scriptIds, data_prim_t(String),  .container = DataContainer_HeapArray, .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitScriptDef, knowledge, t_AssetPrefabValueDef, .container = DataContainer_HeapArray, .flags = DataFlags_Opt);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitBark);
  data_reg_field_t(g_dataReg, AssetPrefabTraitBark, priority, data_prim_t(i32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetPrefabTraitBark, barkDeathPrefab, data_prim_t(StringHash), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitBark, barkConfirmPrefab, data_prim_t(StringHash), .flags = DataFlags_Opt | DataFlags_NotEmpty);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitLocation);
  data_reg_field_t(g_dataReg, AssetPrefabTraitLocation, aimTarget, g_assetGeoBoxType, .flags = DataFlags_Opt);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitStatus);
  data_reg_field_t(g_dataReg, AssetPrefabTraitStatus, supportedStatus, t_AssetPrefabStatusMask, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetPrefabTraitStatus, effectJoint, data_prim_t(StringHash), .flags = DataFlags_Opt | DataFlags_NotEmpty);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitVision);
  data_reg_field_t(g_dataReg, AssetPrefabTraitVision, radius, data_prim_t(f32), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitVision, showInHud, data_prim_t(bool), .flags = DataFlags_Opt);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitAttachment);
  data_reg_field_t(g_dataReg, AssetPrefabTraitAttachment, attachmentPrefab, data_prim_t(StringHash), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitAttachment, attachmentScale, data_prim_t(f32), .flags = DataFlags_NotEmpty | DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetPrefabTraitAttachment, joint, data_prim_t(StringHash), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitAttachment, offset, g_assetGeoVec3Type, .flags = DataFlags_Opt);
  data_reg_normalizer_t(g_dataReg, AssetPrefabTraitAttachment, prefab_data_normalizer_attachment);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitProduction);
  data_reg_field_t(g_dataReg, AssetPrefabTraitProduction, spawnPos, g_assetGeoVec3Type, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetPrefabTraitProduction, rallyPos, g_assetGeoVec3Type, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetPrefabTraitProduction, rallySound, g_assetRefType, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetPrefabTraitProduction, rallySoundGain, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetPrefabTraitProduction, productSetId, data_prim_t(StringHash), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitProduction, placementRadius, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_normalizer_t(g_dataReg, AssetPrefabTraitProduction, prefab_data_normalizer_production);

  data_reg_union_t(g_dataReg, AssetPrefabTraitDef, type);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Name, data_name, t_AssetPrefabTraitName);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_SetMember, data_setMember, t_AssetPrefabTraitSetMember);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Renderable, data_renderable, t_AssetPrefabTraitRenderable);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Vfx, data_vfx, t_AssetPrefabTraitVfx);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Decal, data_decal, t_AssetPrefabTraitDecal);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Sound, data_sound, t_AssetPrefabTraitSoundDef);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_LightPoint, data_lightPoint, t_AssetPrefabTraitLightPoint);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_LightDir, data_lightDir, t_AssetPrefabTraitLightDir);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_LightAmbient, data_lightAmbient, t_AssetPrefabTraitLightAmbient);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Lifetime, data_lifetime, t_AssetPrefabTraitLifetime);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Movement, data_movement, t_AssetPrefabTraitMovement);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Footstep, data_footstep, t_AssetPrefabTraitFootstep);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Health, data_health, t_AssetPrefabTraitHealth);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Attack, data_attack, t_AssetPrefabTraitAttack);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Collision, data_collision, t_AssetPrefabTraitCollisionDef);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Script, data_script, t_AssetPrefabTraitScriptDef);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Bark, data_bark, t_AssetPrefabTraitBark);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Location, data_location, t_AssetPrefabTraitLocation);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Status, data_status, t_AssetPrefabTraitStatus);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Vision, data_vision, t_AssetPrefabTraitVision);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Attachment, data_attachment, t_AssetPrefabTraitAttachment);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Production, data_production, t_AssetPrefabTraitProduction);
  data_reg_choice_empty(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Scalable);

  data_reg_struct_t(g_dataReg, AssetPrefabDef);
  data_reg_field_t(g_dataReg, AssetPrefabDef, name, data_prim_t(String), .flags = DataFlags_NotEmpty | DataFlags_Intern);
  data_reg_field_t(g_dataReg, AssetPrefabDef, isVolatile, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetPrefabDef, traits, t_AssetPrefabTraitDef, .container = DataContainer_HeapArray);

  data_reg_struct_t(g_dataReg, AssetPrefabMapDef);
  data_reg_field_t(g_dataReg, AssetPrefabMapDef, prefabs, t_AssetPrefabDef, .container = DataContainer_HeapArray);
  // clang-format on

  g_assetPrefabDefMeta = data_meta_t(t_AssetPrefabMapDef);
}

void asset_load_prefabs(
    EcsWorld*                 world,
    const AssetImportEnvComp* importEnv,
    const String              id,
    const EcsEntityId         entity,
    AssetSource*              src) {
  (void)importEnv;
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
    const AssetPrefabTrait* trait = &map->traits.values[prefab->traitIndex + i];
    if (trait->type == type) {
      return trait;
    }
  }
  return null;
}
