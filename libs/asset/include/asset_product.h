#pragma once
#include "asset_data.h"
#include "core_unicode.h"
#include "ecs_entity.h"
#include "ecs_module.h"

// Forward declare from 'core_time.h'.
typedef i64 TimeDuration;

/**
 * Product database.
 */

typedef enum {
  AssetProduct_Unit,
  AssetProduct_Placable,
} AssetProductType;

typedef struct {
  StringHash unitPrefab;
  u32        unitCount;
} AssetProductUnit;

typedef struct {
  StringHash prefab;
} AssetProductPlaceable;

typedef struct sAssetProduct {
  String           name;
  AssetProductType type;
  Unicode          icon;
  TimeDuration     costTime;
  TimeDuration     cooldown;
  u16              queueMax;
  u16              queueBulkSize;
  f32              soundReadyGain, soundCancelGain;
  EcsEntityId      soundReady, soundCancel;
  union {
    AssetProductUnit      data_unit;
    AssetProductPlaceable data_placable;
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
