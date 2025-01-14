#pragma once
#include "asset_ref.h"
#include "data.h"
#include "geo_color.h"
#include "geo_quat.h"
#include "geo_vector.h"

typedef enum eAssetPropertyType {
  AssetPropertyType_Num,
  AssetPropertyType_Bool,
  AssetPropertyType_Vec3,
  AssetPropertyType_Quat,
  AssetPropertyType_Color,
  AssetPropertyType_Str,
  AssetPropertyType_LevelEntity,
  AssetPropertyType_Asset,

  AssetPropertyType_Count,
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