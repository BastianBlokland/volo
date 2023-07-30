#pragma once
#include "asset_data.h"
#include "core_unicode.h"
#include "ecs_module.h"

// Forward declare from 'core_time.h'.
typedef i64 TimeDuration;

/**
 * Production database.
 */

typedef enum {
  AssetProduct_Unit,
} AssetProductType;

typedef struct {
  StringHash unitPrefab;
} AssetProductUnit;

typedef struct sAssetProduct {
  AssetProductType type;
  String           name;
  Unicode          icon;
  TimeDuration     costTime;
  u16              queueMax;
  union {
    AssetProductUnit data_unit;
  };
} AssetProduct;

typedef struct {
  StringHash nameHash;
  u16        productIndex, productCount; // Stored in the product array.
} AssetProductSet;

ecs_comp_extern_public(AssetProductMapComp) {
  AssetProductSet* sets; // Sorted on the nameHash.
  usize            setCount;
  AssetProduct*    products;
  usize            productCount;
};

/**
 * Lookup a product-set by the hash of its name.
 */
const AssetProductSet* asset_productset_get(const AssetProductMapComp*, StringHash nameHash);

AssetDataReg asset_product_datareg(void);
