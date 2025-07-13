#pragma once
#include "asset_ref.h"
#include "core_array.h"
#include "data_registry.h"
#include "ecs_module.h"

/**
 * Product database.
 */

typedef struct {
  AssetRef asset;
  f32      gain;
} AssetProductSound;

typedef enum {
  AssetProduct_Unit,
  AssetProduct_Placable,
} AssetProductType;

typedef struct {
  StringHash unitPrefab;
  u32        unitCount;
} AssetProductUnit;

typedef struct {
  StringHash        prefab;
  AssetProductSound soundBlocked;
} AssetProductPlaceable;

typedef struct sAssetProduct {
  String            name;
  AssetProductType  type;
  StringHash        iconImage; // Identifier in the Ui image atlas.
  TimeDuration      costTime;
  TimeDuration      cooldown;
  u16               queueMax;
  u16               queueBulkSize;
  AssetProductSound soundBuilding, soundReady, soundCancel, soundSuccess;
  union {
    AssetProductUnit      data_unit;
    AssetProductPlaceable data_placable;
  };
} AssetProduct;

typedef struct {
  StringHash name;
  u16        productIndex, productCount; // Stored in the product array.
} AssetProductSet;

ecs_comp_extern_public(AssetProductMapComp) {
  HeapArray_t(AssetProductSet) sets; // Sorted on the name.
  HeapArray_t(AssetProduct) products;
};

extern DataMeta g_assetProductDefMeta;

/**
 * Find all asset references in the given product map.
 */
u32 asset_product_refs(const AssetProductMapComp* map, EcsEntityId out[], u32 outMax);

/**
 * Lookup a product-set by the hash of its name.
 */
const AssetProductSet* asset_productset_get(const AssetProductMapComp*, StringHash nameHash);
