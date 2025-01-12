#pragma once
#include "asset.h"
#include "ecs.h"

/**
 * Asset reference.
 */
typedef struct sAssetRef {
  StringHash  id;
  EcsEntityId entity;
} AssetRef;

EcsEntityId asset_ref_resolve(EcsWorld*, AssetManagerComp*, const AssetRef*);
