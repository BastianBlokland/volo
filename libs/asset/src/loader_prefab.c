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
  HeapArray_t(AssetProperty) properties;
} AssetPrefabTraitPropertyDef;

typedef struct {
  bool navBlocker;
  HeapArray_t(AssetPrefabShape) shapes;
} AssetPrefabTraitCollisionDef;

typedef struct {
  AssetRef scripts[asset_prefab_scripts_max];
  HeapArray_t(AssetProperty) properties;
} AssetPrefabTraitScriptDef;

typedef struct {
  AssetPrefabTraitType type;
  union {
    AssetPrefabTraitName         data_name;
    AssetPrefabTraitPropertyDef  data_property;
    AssetPrefabTraitSetMember    data_setMember;
    AssetPrefabTraitRenderable   data_renderable;
    AssetPrefabTraitVfx          data_vfx;
    AssetPrefabTraitDecal        data_decal;
    AssetPrefabTraitSound        data_sound;
    AssetPrefabTraitLightPoint   data_lightPoint;
    AssetPrefabTraitLightSpot    data_lightSpot;
    AssetPrefabTraitLightLine    data_lightLine;
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
  StringHash name;
  bool       isVolatile;
  HeapArray_t(AssetPrefabTraitDef) traits;
} AssetPrefabDef;

typedef struct {
  HeapArray_t(AssetPrefabDef) prefabs;
  HeapArray_t(AssetRef) persistentSounds;
} AssetPrefabMapDef;

static i8 prefab_compare(const void* a, const void* b) {
  return compare_stringhash(field_ptr(a, AssetPrefab, name), field_ptr(b, AssetPrefab, name));
}

typedef enum {
  PrefabError_None,
  PrefabError_DuplicatePrefab,
  PrefabError_DuplicateTrait,
  PrefabError_PrefabCountExceedsMax,
  PrefabError_InvalidAssetReference,

  PrefabError_Count,
} PrefabError;

static String prefab_error_str(const PrefabError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Multiple prefabs with the same name"),
      string_static("Prefab defines the same trait more then once"),
      string_static("Prefab count exceeds the maximum"),
      string_static("Unable to resolve asset-reference"),
  };
  ASSERT(array_elems(g_msgs) == PrefabError_Count, "Incorrect number of error messages");
  return g_msgs[err];
}

static AssetPrefabFlags prefab_build_flags(const AssetPrefabDef* def) {
  AssetPrefabFlags result = 0;
  result |= def->isVolatile ? AssetPrefabFlags_Volatile : 0;
  return result;
}

static void prefab_build(
    const AssetPrefabDef* def,
    DynArray*             outTraits,     // AssetPrefabTrait[], needs to be already initialized.
    DynArray*             outProperties, // AssetProperty[], needs to be already initialized.
    DynArray*             outShapes,     // AssetPrefabShape[], needs to be already initialized.
    AssetPrefab*          outPrefab,
    PrefabError*          err) {

  /**
   * Create in-memory data-structures for a prefab.
   * NOTE: The prefab-hash is not deterministic across sessions as it contains EcsEntityIds.
   */

  const AssetPrefabFlags flags = prefab_build_flags(def);

  *err       = PrefabError_None;
  *outPrefab = (AssetPrefab){
      .name       = def->name,
      .hash       = bits_hash_32_val(flags),
      .flags      = flags,
      .traitIndex = (u16)outTraits->size,
      .traitCount = (u16)def->traits.count,
  };

  const u8     addedTraitsBits[bits_to_bytes(AssetPrefabTrait_Count) + 1] = {0};
  const BitSet addedTraits = bitset_from_array(addedTraitsBits);

  heap_array_for_t(def->traits, AssetPrefabTraitDef, traitDef) {
    if (bitset_test(addedTraits, traitDef->type)) {
      *err = PrefabError_DuplicateTrait;
      return;
    }
    bitset_set(addedTraits, traitDef->type);

#define TRAIT_HASH_ADD(_HASH_) outPrefab->hash = bits_hash_32_combine(outPrefab->hash, _HASH_)

    AssetPrefabTrait* outTrait = dynarray_push_t(outTraits, AssetPrefabTrait);
    outTrait->type             = traitDef->type;
    TRAIT_HASH_ADD(bits_hash_32_val(traitDef->type));

#define TRAIT_EMPTY(_NAME_)                                                                        \
  case AssetPrefabTrait_##_NAME_:                                                                  \
    break

#define TRAIT_COPY(_NAME_, _MEMBER_)                                                               \
  case AssetPrefabTrait_##_NAME_:                                                                  \
    TRAIT_HASH_ADD(bits_hash_32(mem_var(traitDef->_MEMBER_)));                                     \
    outTrait->_MEMBER_ = traitDef->_MEMBER_;                                                       \
    break

    switch (traitDef->type) {
      TRAIT_EMPTY(Scalable);
      TRAIT_COPY(Name, data_name);
      TRAIT_COPY(SetMember, data_setMember);
      TRAIT_COPY(Renderable, data_renderable);
      TRAIT_COPY(Vfx, data_vfx);
      TRAIT_COPY(Decal, data_decal);
      TRAIT_COPY(Sound, data_sound);
      TRAIT_COPY(LightPoint, data_lightPoint);
      TRAIT_COPY(LightSpot, data_lightSpot);
      TRAIT_COPY(LightLine, data_lightLine);
      TRAIT_COPY(LightDir, data_lightDir);
      TRAIT_COPY(LightAmbient, data_lightAmbient);
      TRAIT_COPY(Lifetime, data_lifetime);
      TRAIT_COPY(Movement, data_movement);
      TRAIT_COPY(Footstep, data_footstep);
      TRAIT_COPY(Health, data_health);
      TRAIT_COPY(Attack, data_attack);
      TRAIT_COPY(Bark, data_bark);
      TRAIT_COPY(Location, data_location);
      TRAIT_COPY(Status, data_status);
      TRAIT_COPY(Vision, data_vision);
      TRAIT_COPY(Attachment, data_attachment);
      TRAIT_COPY(Production, data_production);
    case AssetPrefabTrait_Property: {
      const AssetPrefabTraitPropertyDef* propDef   = &traitDef->data_property;
      const u16                          propCount = (u16)propDef->properties.count;
      const Mem propMem = mem_create(propDef->properties.values, sizeof(AssetProperty) * propCount);

      outTrait->data_property = (AssetPrefabTraitProperty){
          .index = (u16)outProperties->size,
          .count = propCount,
      };
      mem_cpy(dynarray_push(outProperties, propCount), propMem);
      TRAIT_HASH_ADD(bits_hash_32(propMem));
    } break;
    case AssetPrefabTrait_Collision: {
      const AssetPrefabTraitCollisionDef* colDef = &traitDef->data_collision;

      const u16 shapeCount = (u16)colDef->shapes.count;
      const Mem shapeMem = mem_create(colDef->shapes.values, sizeof(AssetPrefabShape) * shapeCount);

      outTrait->data_collision = (AssetPrefabTraitCollision){
          .navBlocker = colDef->navBlocker,
          .shapeIndex = (u16)outShapes->size,
          .shapeCount = shapeCount,
      };
      mem_cpy(dynarray_push(outShapes, shapeCount), shapeMem);

      TRAIT_HASH_ADD(bits_hash_32_val(colDef->navBlocker));
      TRAIT_HASH_ADD(bits_hash_32(shapeMem));
    } break;
    case AssetPrefabTrait_Script: {
      const AssetPrefabTraitScriptDef* scriptDef = &traitDef->data_script;

      outTrait->data_script = (AssetPrefabTraitScript){0};
      for (u32 i = 0; i != asset_prefab_scripts_max; ++i) {
        TRAIT_HASH_ADD(scriptDef->scripts[i].id);
        outTrait->data_script.scripts[i] = scriptDef->scripts[i].entity;
      }
    } break;
    case AssetPrefabTrait_Count:
      break;
    }

    if (*err) {
      return; // Failed to build trait.
    }

    // Set prefab flags based on the sets this prefab is a member of.
    if (traitDef->type == AssetPrefabTrait_SetMember) {
      for (u32 i = 0; i != asset_prefab_sets_max; ++i) {
        const StringHash set = outTrait->data_setMember.sets[i];
        if (set) {
          const AssetPrefabFlags setFlags = prefab_set_flags(set);
          outPrefab->flags |= setFlags;
          TRAIT_HASH_ADD(bits_hash_32_val(setFlags));
        }
      }
    }
#undef TRAIT_HASH_ADD
#undef TRAIT_EMPTY
#undef TRAIT_COPY
  }
}

static void prefabmap_build(
    const AssetPrefabMapDef* def,
    DynArray*                outPrefabs,    // AssetPrefab[], needs to be already initialized.
    DynArray*                outTraits,     // AssetPrefabTrait[], needs to be already initialized.
    DynArray*                outProperties, // AssetProperty[], needs to be already initialized.
    DynArray*                outShapes,     // AssetPrefabShape[], needs to be already initialized.
    PrefabError*             err) {

  heap_array_for_t(def->prefabs, AssetPrefabDef, prefabDef) {
    AssetPrefab prefab;
    prefab_build(prefabDef, outTraits, outProperties, outShapes, &prefab, err);
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
 * Build lookups from user-index (index in the source asset array) to prefab-index (and vice versa).
 *
 * Both lookup directions are stored in the same array:
 *  [0 .. prefab-count]                = prefab-index to user-index.
 *  [prefab-count .. prefab-count * 2] = user-index to prefab-index.
 */
static void prefabmap_build_lookups(
    const AssetPrefabMapDef* def,
    const AssetPrefab*       prefabs,      // AssetPrefab[def->prefabs.count]
    u16*                     outUserLookup // u16[def->prefabs.count * 2]
) {
  const u16          prefabCount = (u16)def->prefabs.count;
  const AssetPrefab* prefabsEnd  = prefabs + prefabCount;
  for (u16 userIndex = 0; userIndex != prefabCount; ++userIndex) {
    const StringHash   name   = def->prefabs.values[userIndex].name;
    const AssetPrefab* prefab = search_binary_t(
        prefabs, prefabsEnd, AssetPrefab, prefab_compare, &(AssetPrefab){.name = name});
    diag_assert(prefab && prefab->name == name);
    const u16 prefabIndex                  = (u16)(prefab - prefabs);
    outUserLookup[prefabIndex]             = userIndex;
    outUserLookup[prefabCount + userIndex] = prefabIndex;
  }
}

ecs_comp_define_public(AssetPrefabMapComp);
ecs_comp_define(AssetPrefabLoadComp) { AssetPrefabMapDef def; };

static void ecs_destruct_prefabmap_comp(void* data) {
  AssetPrefabMapComp* comp = data;
  if (comp->prefabs) {
    alloc_free_array_t(g_allocHeap, comp->prefabs, comp->prefabCount);
    alloc_free_array_t(g_allocHeap, comp->userNames, comp->prefabCount); // Strings are interned.
    alloc_free_array_t(g_allocHeap, comp->userLookup, comp->prefabCount * 2);
  }
  if (comp->traits.values) {
    alloc_free_array_t(g_allocHeap, comp->traits.values, comp->traits.count);
  }
  if (comp->properties.values) {
    alloc_free_array_t(g_allocHeap, comp->properties.values, comp->properties.count);
  }
  if (comp->shapes.values) {
    alloc_free_array_t(g_allocHeap, comp->shapes.values, comp->shapes.count);
  }
  if (comp->persistentSounds.values) {
    alloc_free_array_t(g_allocHeap, comp->persistentSounds.values, comp->persistentSounds.count);
  }
}

static void ecs_destruct_prefab_load_comp(void* data) {
  AssetPrefabLoadComp* comp = data;
  data_destroy(g_dataReg, g_allocHeap, g_assetPrefabDefMeta, mem_var(comp->def));
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }

ecs_view_define(LoadView) {
  ecs_access_read(AssetComp);
  ecs_access_write(AssetPrefabLoadComp);
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
    const EcsEntityId    entity = ecs_view_entity(itr);
    const String         id     = asset_id(ecs_view_read_t(itr, AssetComp));
    AssetPrefabLoadComp* load   = ecs_view_write_t(itr, AssetPrefabLoadComp);

    DynArray prefabs    = dynarray_create_t(g_allocHeap, AssetPrefab, 64);
    DynArray traits     = dynarray_create_t(g_allocHeap, AssetPrefabTrait, 64);
    DynArray properties = dynarray_create_t(g_allocHeap, AssetProperty, 64);
    DynArray shapes     = dynarray_create_t(g_allocHeap, AssetPrefabShape, 64);

    String errMsg;
    if (UNLIKELY(load->def.prefabs.count > u16_max)) {
      errMsg = prefab_error_str(PrefabError_PrefabCountExceedsMax);
      goto Error;
    }
    if (!asset_data_patch_refs(world, manager, g_assetPrefabDefMeta, mem_var(load->def))) {
      errMsg = prefab_error_str(PrefabError_InvalidAssetReference);
      goto Error;
    }

    PrefabError buildErr;
    prefabmap_build(&load->def, &prefabs, &traits, &properties, &shapes, &buildErr);
    if (buildErr) {
      errMsg = prefab_error_str(buildErr);
      goto Error;
    }

    String* userNames = prefabs.size ? alloc_array_t(g_allocHeap, String, prefabs.size) : null;
    for (u16 userIndex = 0; userIndex != prefabs.size; ++userIndex) {
      const StringHash nameHash = load->def.prefabs.values[userIndex].name;
      userNames[userIndex]      = stringtable_lookup(g_stringtable, nameHash);
    }

    u16* userLookup = prefabs.size ? alloc_array_t(g_allocHeap, u16, prefabs.size * 2) : null;
    prefabmap_build_lookups(&load->def, dynarray_begin_t(&prefabs, AssetPrefab), userLookup);

    AssetPrefabMapComp* map = ecs_world_add_t(
        world,
        entity,
        AssetPrefabMapComp,
        .prefabs           = dynarray_copy_as_new(&prefabs, g_allocHeap),
        .userNames         = userNames,
        .userLookup        = userLookup,
        .prefabCount       = prefabs.size,
        .traits.values     = dynarray_copy_as_new(&traits, g_allocHeap),
        .traits.count      = traits.size,
        .properties.values = dynarray_copy_as_new(&properties, g_allocHeap),
        .properties.count  = properties.size,
        .shapes.values     = dynarray_copy_as_new(&shapes, g_allocHeap),
        .shapes.count      = shapes.size);

    mem_swap(mem_var(load->def.persistentSounds), mem_var(map->persistentSounds));

    asset_mark_load_success(world, entity);

    goto Cleanup;

  Error:
    asset_mark_load_failure(world, entity, id, errMsg, -1 /* errorCode */);

  Cleanup:
    dynarray_destroy(&prefabs);
    dynarray_destroy(&traits);
    dynarray_destroy(&properties);
    dynarray_destroy(&shapes);
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

static bool prefab_data_normalizer_light_spot(const Mem data) {
  AssetPrefabTraitLightSpot* light = mem_as_t(data, AssetPrefabTraitLightSpot);
  light->angle                     = math_max(0.01f, light->angle) * math_deg_to_rad;
  return true;
}

static bool prefab_data_normalizer_light_line(const Mem data) {
  AssetPrefabTraitLightLine* light = mem_as_t(data, AssetPrefabTraitLightLine);
  light->radius                    = math_max(0.01f, light->radius);
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

static bool prefab_data_normalizer_sound(const Mem data) {
  AssetPrefabTraitSound* sound = mem_as_t(data, AssetPrefabTraitSound);

  sound->gainMin = sound->gainMin < f32_epsilon ? 1.0f : sound->gainMin;
  sound->gainMax = math_max(sound->gainMin, sound->gainMax);

  sound->pitchMin = sound->pitchMin < f32_epsilon ? 1.0f : sound->pitchMin;
  sound->pitchMax = math_max(sound->pitchMin, sound->pitchMax);

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

  data_reg_struct_t(g_dataReg, AssetPrefabTraitName);
  data_reg_field_t(g_dataReg, AssetPrefabTraitName, name, data_prim_t(StringHash), .flags = DataFlags_NotEmpty);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitPropertyDef);
  data_reg_field_t(g_dataReg, AssetPrefabTraitPropertyDef, properties, g_assetPropertyType, .container = DataContainer_HeapArray);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitSetMember);
  data_reg_field_t(g_dataReg, AssetPrefabTraitSetMember, sets, data_prim_t(StringHash), .container = DataContainer_InlineArray, .fixedCount = asset_prefab_sets_max, .flags = DataFlags_NotEmpty);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitRenderable);
  data_reg_field_t(g_dataReg, AssetPrefabTraitRenderable, graphic, g_assetRefType);
  data_reg_field_t(g_dataReg, AssetPrefabTraitRenderable, emissive, data_prim_t(f32), .flags = DataFlags_Opt);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitVfx);
  data_reg_field_t(g_dataReg, AssetPrefabTraitVfx, asset, g_assetRefType);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitDecal);
  data_reg_field_t(g_dataReg, AssetPrefabTraitDecal, asset, g_assetRefType);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitSound);
  data_reg_field_t(g_dataReg, AssetPrefabTraitSound, assets, g_assetRefType, .container = DataContainer_InlineArray, .fixedCount = asset_prefab_sounds_max, .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitSound, gainMin, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitSound, gainMax, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitSound, pitchMin, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitSound, pitchMax, data_prim_t(f32), .flags = DataFlags_Opt | DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitSound, looping, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_normalizer_t(g_dataReg, AssetPrefabTraitSound, prefab_data_normalizer_sound);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitLightPoint);
  data_reg_field_t(g_dataReg, AssetPrefabTraitLightPoint, radiance, g_assetGeoColor4Type, .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitLightPoint, radius, data_prim_t(f32), .flags = DataFlags_NotEmpty);
  data_reg_normalizer_t(g_dataReg, AssetPrefabTraitLightPoint, prefab_data_normalizer_light_point);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitLightSpot);
  data_reg_field_t(g_dataReg, AssetPrefabTraitLightSpot, radiance, g_assetGeoColor4Type, .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitLightSpot, angle, data_prim_t(f32), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitLightSpot, length, data_prim_t(f32), .flags = DataFlags_NotEmpty);
  data_reg_normalizer_t(g_dataReg, AssetPrefabTraitLightSpot, prefab_data_normalizer_light_spot);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitLightLine);
  data_reg_field_t(g_dataReg, AssetPrefabTraitLightLine, radiance, g_assetGeoColor4Type, .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitLightLine, radius, data_prim_t(f32), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitLightLine, length, data_prim_t(f32), .flags = DataFlags_NotEmpty);
  data_reg_normalizer_t(g_dataReg, AssetPrefabTraitLightLine, prefab_data_normalizer_light_line);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitLightDir);
  data_reg_field_t(g_dataReg, AssetPrefabTraitLightDir, radiance, g_assetGeoColor4Type, .flags = DataFlags_NotEmpty);
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
  data_reg_field_t(g_dataReg, AssetPrefabTraitCollisionDef, shapes, t_AssetPrefabShape, .container = DataContainer_HeapArray, .flags = DataFlags_NotEmpty);

  data_reg_struct_t(g_dataReg, AssetPrefabTraitScriptDef);
  data_reg_field_t(g_dataReg, AssetPrefabTraitScriptDef, scripts, g_assetRefType,  .container = DataContainer_InlineArray, .fixedCount = asset_prefab_scripts_max, .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabTraitScriptDef, properties, g_assetPropertyType, .container = DataContainer_HeapArray, .flags = DataFlags_Opt);

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
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Property, data_property, t_AssetPrefabTraitPropertyDef);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_SetMember, data_setMember, t_AssetPrefabTraitSetMember);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Renderable, data_renderable, t_AssetPrefabTraitRenderable);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Vfx, data_vfx, t_AssetPrefabTraitVfx);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Decal, data_decal, t_AssetPrefabTraitDecal);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_Sound, data_sound, t_AssetPrefabTraitSound);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_LightPoint, data_lightPoint, t_AssetPrefabTraitLightPoint);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_LightSpot, data_lightSpot, t_AssetPrefabTraitLightSpot);
  data_reg_choice_t(g_dataReg, AssetPrefabTraitDef, AssetPrefabTrait_LightLine, data_lightLine, t_AssetPrefabTraitLightLine);
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
  data_reg_field_t(g_dataReg, AssetPrefabDef, name, data_prim_t(StringHash), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetPrefabDef, isVolatile, data_prim_t(bool), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetPrefabDef, traits, t_AssetPrefabTraitDef, .container = DataContainer_HeapArray);

  data_reg_struct_t(g_dataReg, AssetPrefabMapDef);
  data_reg_field_t(g_dataReg, AssetPrefabMapDef, prefabs, t_AssetPrefabDef, .container = DataContainer_HeapArray);
  data_reg_field_t(g_dataReg, AssetPrefabMapDef, persistentSounds, g_assetRefType, .container = DataContainer_HeapArray, .flags = DataFlags_Opt);
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

  AssetPrefabMapDef def;
  DataReadResult    result;
  if (src->format == AssetFormat_PrefabsBin) {
    data_read_bin(g_dataReg, src->data, g_allocHeap, g_assetPrefabDefMeta, mem_var(def), &result);
  } else {
    data_read_json(g_dataReg, src->data, g_allocHeap, g_assetPrefabDefMeta, mem_var(def), &result);
  }

  if (UNLIKELY(result.error)) {
    asset_mark_load_failure(world, entity, id, result.errorMsg, (i32)result.error);
    goto Ret;
  }

  if (src->format != AssetFormat_PrefabsBin) {
    // TODO: Instead of caching the definition it would be more optimal to cache the resulting map.
    asset_cache(world, entity, g_assetPrefabDefMeta, mem_var(def));
  }

  ecs_world_add_t(world, entity, AssetPrefabLoadComp, .def = def);

Ret:
  asset_repo_close(src);
}

u32 asset_prefab_refs(const AssetPrefabMapComp* map, EcsEntityId out[], const u32 outMax) {
#define PUSH_ASSET(_ASSET_REF_)                                                                    \
  if ((_ASSET_REF_).entity && outCount != outMax) {                                                \
    out[outCount++] = (_ASSET_REF_).entity;                                                        \
  }

  u32 outCount = 0;
  for (u32 traitIdx = 0; traitIdx != map->traits.count && outCount != outMax; ++traitIdx) {
    const AssetPrefabTrait* trait = &map->traits.values[traitIdx];
    switch (trait->type) {
    case AssetPrefabTrait_Renderable:
      PUSH_ASSET(trait->data_renderable.graphic);
      break;
    case AssetPrefabTrait_Vfx:
      PUSH_ASSET(trait->data_vfx.asset);
      break;
    case AssetPrefabTrait_Decal:
      PUSH_ASSET(trait->data_decal.asset);
      break;
    case AssetPrefabTrait_Sound:
      for (u32 soundIdx = 0; soundIdx != asset_prefab_sounds_max; ++soundIdx) {
        PUSH_ASSET(trait->data_sound.assets[soundIdx]);
      }
      break;
    case AssetPrefabTrait_Footstep:
      PUSH_ASSET(trait->data_footstep.decalA);
      PUSH_ASSET(trait->data_footstep.decalB);
      break;
    case AssetPrefabTrait_Production:
      PUSH_ASSET(trait->data_production.rallySound);
      break;
    case AssetPrefabTrait_Script:
      for (u32 scriptIdx = 0; scriptIdx != asset_prefab_scripts_max; ++scriptIdx) {
        if (trait->data_script.scripts[scriptIdx] && outCount != outMax) {
          out[outCount++] = trait->data_script.scripts[scriptIdx];
        }
      }
      break;
    case AssetPrefabTrait_Name:
    case AssetPrefabTrait_Property:
    case AssetPrefabTrait_SetMember:
    case AssetPrefabTrait_LightPoint:
    case AssetPrefabTrait_LightSpot:
    case AssetPrefabTrait_LightLine:
    case AssetPrefabTrait_LightDir:
    case AssetPrefabTrait_LightAmbient:
    case AssetPrefabTrait_Lifetime:
    case AssetPrefabTrait_Movement:
    case AssetPrefabTrait_Health:
    case AssetPrefabTrait_Attack:
    case AssetPrefabTrait_Collision:
    case AssetPrefabTrait_Bark:
    case AssetPrefabTrait_Location:
    case AssetPrefabTrait_Status:
    case AssetPrefabTrait_Vision:
    case AssetPrefabTrait_Attachment:
    case AssetPrefabTrait_Scalable:
    case AssetPrefabTrait_Count:
      break;
    }
  }
  for (u32 propIdx = 0; propIdx != map->properties.count && outCount != outMax; ++propIdx) {
    if (map->properties.values[propIdx].type == AssetProperty_Asset) {
      PUSH_ASSET(map->properties.values[propIdx].data_asset);
    }
  }
  for (u32 sndIdx = 0; sndIdx != map->persistentSounds.count && outCount != outMax; ++sndIdx) {
    PUSH_ASSET(map->persistentSounds.values[sndIdx]);
  }
  return outCount;
}

const AssetPrefab* asset_prefab_find(const AssetPrefabMapComp* map, const StringHash name) {
  return search_binary_t(
      map->prefabs,
      map->prefabs + map->prefabCount,
      AssetPrefab,
      prefab_compare,
      mem_struct(AssetPrefab, .name = name).ptr);
}

u16 asset_prefab_find_index(const AssetPrefabMapComp* map, const StringHash name) {
  const AssetPrefab* prefab = asset_prefab_find(map, name);
  if (UNLIKELY(!prefab)) {
    return sentinel_u16;
  }
  return (u16)(prefab - map->prefabs);
}

u16 asset_prefab_index_to_user(const AssetPrefabMapComp* map, const u16 prefabIndex) {
  diag_assert(prefabIndex < map->prefabCount);
  return map->userLookup[prefabIndex];
}

u16 asset_prefab_index_from_user(const AssetPrefabMapComp* map, const u16 userIndex) {
  diag_assert(userIndex < map->prefabCount);
  return map->userLookup[map->prefabCount + userIndex];
}

const AssetPrefabTrait* asset_prefab_trait(
    const AssetPrefabMapComp* map, const AssetPrefab* prefab, const AssetPrefabTraitType type) {
  for (u16 i = 0; i != prefab->traitCount; ++i) {
    const AssetPrefabTrait* trait = &map->traits.values[prefab->traitIndex + i];
    if (trait->type == type) {
      return trait;
    }
  }
  return null;
}
