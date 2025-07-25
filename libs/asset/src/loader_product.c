#include "asset_product.h"
#include "core.h"
#include "core_alloc.h"
#include "core_dynarray.h"
#include "core_math.h"
#include "core_search.h"
#include "core_time.h"
#include "data_read.h"
#include "data_utils.h"
#include "ecs_entity.h"
#include "ecs_utils.h"
#include "ecs_view.h"

#include "data_internal.h"
#include "import_internal.h"
#include "manager_internal.h"
#include "repo_internal.h"

DataMeta g_assetProductDefMeta;

typedef struct {
  String            name;
  StringHash        iconImage;
  TimeDuration      costTime;
  u16               queueMax;
  u16               queueBulkSize;
  TimeDuration      cooldown;
  AssetProductSound soundBuilding, soundReady, soundCancel, soundSuccess;
} AssetProductMetaDef;

typedef struct {
  AssetProductMetaDef meta;
  StringHash          unitPrefab;
  u32                 unitCount;
} AssetProductUnitDef;

typedef struct {
  AssetProductMetaDef meta;
  StringHash          prefab;
  AssetProductSound   soundBlocked;
} AssetProductPlacableDef;

typedef struct {
  AssetProductType type;
  union {
    AssetProductUnitDef     data_unit;
    AssetProductPlacableDef data_placable;
  };
} AssetProductDef;

typedef struct {
  StringHash name;
  HeapArray_t(AssetProductDef) products;
} AssetProductSetDef;

typedef struct {
  HeapArray_t(AssetProductSetDef) sets;
} AssetProductMapDef;

static i8 asset_productset_compare(const void* a, const void* b) {
  return compare_stringhash(
      field_ptr(a, AssetProductSet, name), field_ptr(b, AssetProductSet, name));
}

typedef enum {
  ProductError_None,
  ProductError_DuplicateProductSet,
  ProductError_EmptyProductSet,
  ProductError_InvalidAssetReference,

  ProductError_Count,
} ProductError;

static String product_error_str(const ProductError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Multiple product-sets with the same name"),
      string_static("Product-set cannot be empty"),
      string_static("Unable to resolve asset-reference"),
  };
  ASSERT(array_elems(g_msgs) == ProductError_Count, "Incorrect number of error messages");
  return g_msgs[err];
}

static void product_build_meta(const AssetProductMetaDef* def, AssetProduct* out) {
  out->iconImage     = def->iconImage;
  out->name          = string_maybe_dup(g_allocHeap, def->name);
  out->costTime      = def->costTime;
  out->queueMax      = def->queueMax;
  out->queueBulkSize = def->queueBulkSize;
  out->cooldown      = def->cooldown;
  out->soundBuilding = def->soundBuilding;
  out->soundReady    = def->soundReady;
  out->soundCancel   = def->soundCancel;
  out->soundSuccess  = def->soundSuccess;
}

static void productset_build(
    const AssetProductSetDef* def,
    DynArray*                 outProducts, // AssetProduct[], needs to be already initialized.
    AssetProductSet*          outSet,
    ProductError*             err) {

  if (!def->products.count) {
    *err = ProductError_EmptyProductSet;
    return;
  }

  *err    = ProductError_None;
  *outSet = (AssetProductSet){
      .name         = def->name,
      .productIndex = (u16)outProducts->size,
      .productCount = (u16)def->products.count,
  };

  heap_array_for_t(def->products, AssetProductDef, productDef) {
    AssetProduct* outProduct = dynarray_push_t(outProducts, AssetProduct);
    outProduct->type         = productDef->type;

    switch (productDef->type) {
    case AssetProduct_Unit:
      product_build_meta(&productDef->data_unit.meta, outProduct);
      outProduct->data_unit = (AssetProductUnit){
          .unitPrefab = productDef->data_unit.unitPrefab,
          .unitCount  = productDef->data_unit.unitCount,
      };
      break;
    case AssetProduct_Placable: {
      const AssetProductPlacableDef* placeDef = &productDef->data_placable;
      product_build_meta(&placeDef->meta, outProduct);
      outProduct->data_placable = (AssetProductPlaceable){
          .prefab       = placeDef->prefab,
          .soundBlocked = placeDef->soundBlocked,
      };
    } break;
    }
    if (*err) {
      return; // Failed to build product-set.
    }
  }
}

static void productmap_build(
    const AssetProductMapDef* def,
    DynArray*                 outSets,     // AssetProductSet[], needs to be already initialized.
    DynArray*                 outProducts, // AssetProduct[], needs to be already initialized.
    ProductError*             err) {

  heap_array_for_t(def->sets, AssetProductSetDef, setDef) {
    AssetProductSet set;
    productset_build(setDef, outProducts, &set, err);
    if (*err) {
      return;
    }
    if (dynarray_search_binary(outSets, asset_productset_compare, &set)) {
      *err = ProductError_DuplicateProductSet;
      return;
    }
    *dynarray_insert_sorted_t(outSets, AssetProductSet, asset_productset_compare, &set) = set;
  }
  *err = ProductError_None;
}

ecs_comp_define_public(AssetProductMapComp);
ecs_comp_define(AssetProductLoadComp) { AssetProductMapDef def; };

static void ecs_destruct_productmap_comp(void* data) {
  AssetProductMapComp* comp = data;
  if (comp->sets.values) {
    alloc_free_array_t(g_allocHeap, comp->sets.values, comp->sets.count);
  }
  if (comp->products.values) {
    for (u32 i = 0; i != comp->products.count; ++i) {
      string_maybe_free(g_allocHeap, comp->products.values[i].name);
    }
    alloc_free_array_t(g_allocHeap, comp->products.values, comp->products.count);
  }
}

static void ecs_destruct_product_load_comp(void* data) {
  AssetProductLoadComp* comp = data;
  data_destroy(g_dataReg, g_allocHeap, g_assetProductDefMeta, mem_var(comp->def));
}

static bool product_data_normalizer_metadef(const Mem data) {
  AssetProductMetaDef* meta = mem_as_t(data, AssetProductMetaDef);
  meta->costTime            = math_max(meta->costTime, time_millisecond);
  meta->queueMax            = meta->queueMax ? meta->queueMax : u16_max;
  meta->queueBulkSize       = meta->queueBulkSize ? meta->queueBulkSize : 5;
  meta->cooldown            = math_max(meta->cooldown, time_millisecond);
  return true;
}

static bool product_data_normalizer_unit(const Mem data) {
  AssetProductUnitDef* unit = mem_as_t(data, AssetProductUnitDef);
  unit->unitCount           = math_max(1, unit->unitCount);
  return true;
}

static bool product_data_normalizer_sound(const Mem data) {
  AssetProductSound* snd = mem_as_t(data, AssetProductSound);
  snd->gain              = snd->gain <= 0.0f ? 1.0f : snd->gain;
  return true;
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }

ecs_view_define(LoadView) {
  ecs_access_read(AssetComp);
  ecs_access_write(AssetProductLoadComp);
}

ecs_view_define(UnloadView) {
  ecs_access_with(AssetProductMapComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Load product-map assets.
 */
ecs_system_define(LoadProductAssetSys) {
  AssetManagerComp* manager = ecs_utils_write_first_t(world, ManagerView, AssetManagerComp);
  if (!manager) {
    return;
  }

  EcsView* loadView = ecs_world_view_t(world, LoadView);
  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId     entity = ecs_view_entity(itr);
    const String          id     = asset_id(ecs_view_read_t(itr, AssetComp));
    AssetProductLoadComp* load   = ecs_view_write_t(itr, AssetProductLoadComp);

    DynArray sets     = dynarray_create_t(g_allocHeap, AssetProductSet, 64);
    DynArray products = dynarray_create_t(g_allocHeap, AssetProduct, 64);

    String errMsg;
    if (!asset_data_patch_refs(world, manager, g_assetProductDefMeta, mem_var(load->def))) {
      errMsg = product_error_str(ProductError_InvalidAssetReference);
      goto Error;
    }

    ProductError buildErr;
    productmap_build(&load->def, &sets, &products, &buildErr);
    if (buildErr) {
      errMsg = product_error_str(buildErr);
      goto Error;
    }

    ecs_world_add_t(
        world,
        entity,
        AssetProductMapComp,
        .sets.values     = dynarray_copy_as_new(&sets, g_allocHeap),
        .sets.count      = sets.size,
        .products.values = dynarray_copy_as_new(&products, g_allocHeap),
        .products.count  = products.size);

    asset_mark_load_success(world, entity);
    goto Cleanup;

  Error:
    dynarray_for_t(&products, AssetProduct, prod) { string_maybe_free(g_allocHeap, prod->name); }
    asset_mark_load_failure(world, entity, id, errMsg, -1 /* errorCode */);

  Cleanup:
    dynarray_destroy(&sets);
    dynarray_destroy(&products);
    ecs_world_remove_t(world, entity, AssetProductLoadComp);
  }
}

/**
 * Remove any product-map asset component for unloaded assets.
 */
ecs_system_define(UnloadProductAssetSys) {
  EcsView* unloadView = ecs_world_view_t(world, UnloadView);
  for (EcsIterator* itr = ecs_view_itr(unloadView); ecs_view_walk(itr);) {
    const EcsEntityId entity = ecs_view_entity(itr);
    ecs_world_remove_t(world, entity, AssetProductMapComp);
  }
}

ecs_module_init(asset_product_module) {
  ecs_register_comp(AssetProductMapComp, .destructor = ecs_destruct_productmap_comp);
  ecs_register_comp(AssetProductLoadComp, .destructor = ecs_destruct_product_load_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(UnloadView);

  ecs_register_system(LoadProductAssetSys, ecs_view_id(ManagerView), ecs_view_id(LoadView));
  ecs_register_system(UnloadProductAssetSys, ecs_view_id(UnloadView));
}

void asset_data_init_product(void) {
  // clang-format off
  data_reg_struct_t(g_dataReg, AssetProductSound);
  data_reg_field_t(g_dataReg, AssetProductSound, asset, g_assetRefType);
  data_reg_field_t(g_dataReg, AssetProductSound, gain, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_normalizer_t(g_dataReg, AssetProductSound, product_data_normalizer_sound);

  data_reg_struct_t(g_dataReg, AssetProductMetaDef);
  data_reg_field_t(g_dataReg, AssetProductMetaDef, name, data_prim_t(String), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetProductMetaDef, iconImage, data_prim_t(StringHash), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetProductMetaDef, costTime, data_prim_t(TimeDuration), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetProductMetaDef, queueMax, data_prim_t(u16), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetProductMetaDef, queueBulkSize, data_prim_t(u16), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetProductMetaDef, cooldown, data_prim_t(TimeDuration), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetProductMetaDef, soundBuilding, t_AssetProductSound, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetProductMetaDef, soundReady, t_AssetProductSound, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetProductMetaDef, soundCancel, t_AssetProductSound, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetProductMetaDef, soundSuccess, t_AssetProductSound, .flags = DataFlags_Opt);
  data_reg_normalizer_t(g_dataReg, AssetProductMetaDef, product_data_normalizer_metadef);

  data_reg_struct_t(g_dataReg, AssetProductUnitDef);
  data_reg_field_t(g_dataReg, AssetProductUnitDef, meta, t_AssetProductMetaDef, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetProductUnitDef, unitPrefab, data_prim_t(StringHash), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetProductUnitDef, unitCount, data_prim_t(u32), .flags = DataFlags_NotEmpty | DataFlags_Opt);
  data_reg_normalizer_t(g_dataReg, AssetProductUnitDef, product_data_normalizer_unit);

  data_reg_struct_t(g_dataReg, AssetProductPlacableDef);
  data_reg_field_t(g_dataReg, AssetProductPlacableDef, meta, t_AssetProductMetaDef, .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, AssetProductPlacableDef, prefab, data_prim_t(StringHash), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetProductPlacableDef, soundBlocked, t_AssetProductSound, .flags = DataFlags_Opt);

  data_reg_union_t(g_dataReg, AssetProductDef, type);
  data_reg_choice_t(g_dataReg, AssetProductDef, AssetProduct_Unit, data_unit, t_AssetProductUnitDef);
  data_reg_choice_t(g_dataReg, AssetProductDef, AssetProduct_Placable, data_placable, t_AssetProductPlacableDef);

  data_reg_struct_t(g_dataReg, AssetProductSetDef);
  data_reg_field_t(g_dataReg, AssetProductSetDef, name, data_prim_t(StringHash), .flags = DataFlags_NotEmpty);
  data_reg_field_t(g_dataReg, AssetProductSetDef, products, t_AssetProductDef, .container = DataContainer_HeapArray, .flags = DataFlags_NotEmpty);

  data_reg_struct_t(g_dataReg, AssetProductMapDef);
  data_reg_field_t(g_dataReg, AssetProductMapDef, sets, t_AssetProductSetDef, .container = DataContainer_HeapArray);
  // clang-format on

  g_assetProductDefMeta = data_meta_t(t_AssetProductMapDef);
}

void asset_load_products(
    EcsWorld*                 world,
    const AssetImportEnvComp* importEnv,
    const String              id,
    const EcsEntityId         entity,
    AssetSource*              src) {
  (void)importEnv;
  (void)id;

  AssetProductMapDef def;
  DataReadResult     result;
  if (src->format == AssetFormat_ProductsBin) {
    data_read_bin(g_dataReg, src->data, g_allocHeap, g_assetProductDefMeta, mem_var(def), &result);
  } else {
    data_read_json(g_dataReg, src->data, g_allocHeap, g_assetProductDefMeta, mem_var(def), &result);
  }

  if (UNLIKELY(result.error)) {
    asset_mark_load_failure(world, entity, id, result.errorMsg, (i32)result.error);
    goto Ret;
  }

  if (src->format != AssetFormat_ProductsBin) {
    // TODO: Instead of caching the definition it would be more optimal to cache the resulting map.
    asset_cache(world, entity, g_assetProductDefMeta, mem_var(def));
  }

  ecs_world_add_t(world, entity, AssetProductLoadComp, .def = def);

Ret:
  asset_repo_close(src);
}

u32 asset_product_refs(const AssetProductMapComp* map, EcsEntityId out[], const u32 outMax) {
#define PUSH_SOUND(_SND_)                                                                          \
  if ((_SND_).asset.entity && outCount != outMax) {                                                \
    out[outCount++] = (_SND_).asset.entity;                                                        \
  }

  u32 outCount = 0;
  for (u32 i = 0; i != map->products.count && outCount != outMax; ++i) {
    const AssetProduct* product = &map->products.values[i];
    PUSH_SOUND(product->soundBuilding);
    PUSH_SOUND(product->soundReady);
    PUSH_SOUND(product->soundCancel);
    PUSH_SOUND(product->soundSuccess);
    switch (product->type) {
    case AssetProduct_Unit:
      break;
    case AssetProduct_Placable:
      PUSH_SOUND(product->data_placable.soundBlocked);
      break;
    }
  }

#undef PUSH_SOUND
  return outCount;
}

const AssetProductSet*
asset_productset_get(const AssetProductMapComp* map, const StringHash nameHash) {
  return search_binary_t(
      map->sets.values,
      map->sets.values + map->sets.count,
      AssetProductSet,
      asset_productset_compare,
      mem_struct(AssetProductSet, .name = nameHash).ptr);
}
