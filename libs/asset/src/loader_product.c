#include "asset_product.h"
#include "core_alloc.h"
#include "core_annotation.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_search.h"
#include "core_stringtable.h"
#include "core_thread.h"
#include "data.h"
#include "ecs_utils.h"
#include "log_logger.h"

#include "repo_internal.h"

static DataReg* g_dataReg;
static DataMeta g_dataMapDefMeta;

typedef struct {
  String unitPrefab;
} AssetProductUnitDef;

typedef struct {
  AssetProductType type;
  union {
    AssetProductUnitDef data_unit;
  };
} AssetProductDef;

typedef struct {
  String name;
  struct {
    AssetProductDef* values;
    usize            count;
  } products;
} AssetProductSetDef;

typedef struct {
  struct {
    AssetProductSetDef* values;
    usize               count;
  } sets;
} AssetProductMapDef;

static void product_datareg_init() {
  static ThreadSpinLock g_initLock;
  if (LIKELY(g_dataReg)) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_dataReg) {
    DataReg* reg = data_reg_create(g_alloc_persist);

    // clang-format off
    data_reg_struct_t(reg, AssetProductUnitDef);
    data_reg_field_t(reg, AssetProductUnitDef, unitPrefab, data_prim_t(String), .flags = DataFlags_NotEmpty);

    data_reg_union_t(reg, AssetProductDef, type);
    data_reg_choice_t(reg, AssetProductDef, AssetProduct_Unit, data_unit, t_AssetProductUnitDef);

    data_reg_struct_t(reg, AssetProductSetDef);
    data_reg_field_t(reg, AssetProductSetDef, name, data_prim_t(String), .flags = DataFlags_NotEmpty);
    data_reg_field_t(reg, AssetProductSetDef, products, t_AssetProductDef, .container = DataContainer_Array, .flags = DataFlags_NotEmpty);

    data_reg_struct_t(reg, AssetProductMapDef);
    data_reg_field_t(reg, AssetProductMapDef, sets, t_AssetProductSetDef, .container = DataContainer_Array);
    // clang-format on

    g_dataMapDefMeta = data_meta_t(t_AssetProductMapDef);
    g_dataReg        = reg;
  }
  thread_spinlock_unlock(&g_initLock);
}

static i8 asset_productset_compare(const void* a, const void* b) {
  return compare_stringhash(
      field_ptr(a, AssetProductSet, nameHash), field_ptr(b, AssetProductSet, nameHash));
}

typedef enum {
  ProductError_None                = 0,
  ProductError_DuplicateProductSet = 1,
  ProductError_EmptyProductSet     = 2,

  ProductError_Count,
} ProductError;

static String product_error_str(const ProductError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Multiple product-sets with the same name"),
      string_static("Product-set cannot be empty"),
  };
  ASSERT(array_elems(g_msgs) == ProductError_Count, "Incorrect number of error messages");
  return g_msgs[err];
}

static void
product_unit_build(const AssetProductUnitDef* def, AssetProductUnit* out, ProductError* err) {
  *out = (AssetProductUnit){
      .unitPrefab = string_hash(def->unitPrefab),
  };
  *err = ProductError_None;
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
      .nameHash     = stringtable_add(g_stringtable, def->name),
      .productIndex = (u16)outProducts->size,
      .productCount = (u16)def->products.count,
  };

  array_ptr_for_t(def->products, AssetProductDef, productDef) {
    AssetProduct* outProduct = dynarray_push_t(outProducts, AssetProduct);
    outProduct->type         = productDef->type;

    switch (productDef->type) {
    case AssetProduct_Unit:
      product_unit_build(&productDef->data_unit, &outProduct->data_unit, err);
      break;
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

  array_ptr_for_t(def->sets, AssetProductSetDef, setDef) {
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
ecs_comp_define(AssetProductLoadComp) { AssetSource* src; };

static void ecs_destruct_productmap_comp(void* data) {
  AssetProductMapComp* comp = data;
  if (comp->sets) {
    alloc_free_array_t(g_alloc_heap, comp->sets, comp->setCount);
  }
  if (comp->products) {
    alloc_free_array_t(g_alloc_heap, comp->products, comp->productCount);
  }
}

static void ecs_destruct_product_load_comp(void* data) {
  AssetProductLoadComp* comp = data;
  asset_repo_source_close(comp->src);
}

ecs_view_define(ManagerView) { ecs_access_write(AssetManagerComp); }
ecs_view_define(LoadView) { ecs_access_read(AssetProductLoadComp); }

ecs_view_define(UnloadView) {
  ecs_access_with(AssetProductMapComp);
  ecs_access_without(AssetLoadedComp);
}

/**
 * Load product-map assets.
 */
ecs_system_define(LoadProductAssetSys) {
  EcsView* loadView = ecs_world_view_t(world, LoadView);
  for (EcsIterator* itr = ecs_view_itr(loadView); ecs_view_walk(itr);) {
    const EcsEntityId  entity = ecs_view_entity(itr);
    const AssetSource* src    = ecs_view_read_t(itr, AssetProductLoadComp)->src;

    DynArray sets     = dynarray_create_t(g_alloc_heap, AssetProductSet, 64);
    DynArray products = dynarray_create_t(g_alloc_heap, AssetProduct, 64);

    AssetProductMapDef def;
    String             errMsg;
    DataReadResult     readRes;
    data_read_json(g_dataReg, src->data, g_alloc_heap, g_dataMapDefMeta, mem_var(def), &readRes);
    if (UNLIKELY(readRes.error)) {
      errMsg = readRes.errorMsg;
      goto Error;
    }

    ProductError buildErr;
    productmap_build(&def, &sets, &products, &buildErr);
    data_destroy(g_dataReg, g_alloc_heap, g_dataMapDefMeta, mem_var(def));
    if (buildErr) {
      errMsg = product_error_str(buildErr);
      goto Error;
    }

    ecs_world_add_t(
        world,
        entity,
        AssetProductMapComp,
        .sets         = dynarray_copy_as_new(&sets, g_alloc_heap),
        .setCount     = sets.size,
        .products     = dynarray_copy_as_new(&products, g_alloc_heap),
        .productCount = products.size);

    ecs_world_add_empty_t(world, entity, AssetLoadedComp);
    goto Cleanup;

  Error:
    log_e("Failed to load ProductMap", log_param("error", fmt_text(errMsg)));
    ecs_world_add_empty_t(world, entity, AssetFailedComp);

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
  product_datareg_init();

  ecs_register_comp(AssetProductMapComp, .destructor = ecs_destruct_productmap_comp);
  ecs_register_comp(AssetProductLoadComp, .destructor = ecs_destruct_product_load_comp);

  ecs_register_view(ManagerView);
  ecs_register_view(LoadView);
  ecs_register_view(UnloadView);

  ecs_register_system(LoadProductAssetSys, ecs_view_id(ManagerView), ecs_view_id(LoadView));
  ecs_register_system(UnloadProductAssetSys, ecs_view_id(UnloadView));
}

void asset_load_pro(EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {
  (void)id;
  ecs_world_add_t(world, entity, AssetProductLoadComp, .src = src);
}

const AssetProductSet*
asset_productset_get(const AssetProductMapComp* map, const StringHash nameHash) {
  return search_binary_t(
      map->sets,
      map->sets + map->setCount,
      AssetProductSet,
      asset_productset_compare,
      mem_struct(AssetProductSet, .nameHash = nameHash).ptr);
}

AssetDataReg asset_product_datareg() {
  product_datareg_init();
  return (AssetDataReg){
      .registry = g_dataReg,
      .typeMeta = g_dataMapDefMeta,
  };
}
