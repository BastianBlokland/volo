#pragma once
#include "asset_ref.h"
#include "data.h"
#include "geo_color.h"
#include "geo_quat.h"
#include "geo_vector.h"

typedef enum {
  AssetProperty_Num,
  AssetProperty_Bool,
  AssetProperty_Vec3,
  AssetProperty_Quat,
  AssetProperty_Color,
  AssetProperty_Str,
  AssetProperty_LevelEntity,
  AssetProperty_Asset,

  AssetProperty_Count,
} AssetPropertyType;

typedef struct sAssetProperty {
  StringHash        name;
  AssetPropertyType type;
  union {
    f64           data_num;
    bool          data_bool;
    GeoVector     data_vec3;
    GeoQuat       data_quat;
    GeoColor      data_color;
    StringHash    data_str;
    AssetLevelRef data_levelEntity;
    AssetRef      data_asset;
  };
} AssetProperty;

extern DataType g_assetPropertyType;
