#include "asset/property.h"
#include "data/registry.h"

#include "data.h"

DataType g_assetPropertyType;

void asset_data_init_property(void) {
  // clang-format off
  data_reg_union_t(g_dataReg, AssetProperty, type);
  data_reg_union_name_t(g_dataReg, AssetProperty, name, DataUnionNameType_StringHash);
  data_reg_choice_t(g_dataReg, AssetProperty, AssetProperty_Num, data_num, data_prim_t(f64));
  data_reg_choice_t(g_dataReg, AssetProperty, AssetProperty_Bool, data_bool, data_prim_t(bool));
  data_reg_choice_t(g_dataReg, AssetProperty, AssetProperty_Vec3, data_vec3, g_assetGeoVec3Type);
  data_reg_choice_t(g_dataReg, AssetProperty, AssetProperty_Quat, data_quat, g_assetGeoQuatType);
  data_reg_choice_t(g_dataReg, AssetProperty, AssetProperty_Color, data_color, g_assetGeoColor4Type);
  data_reg_choice_t(g_dataReg, AssetProperty, AssetProperty_Str, data_str, data_prim_t(StringHash));
  data_reg_choice_empty(g_dataReg, AssetProperty, AssetProperty_EntitySelf);
  data_reg_choice_t(g_dataReg, AssetProperty, AssetProperty_EntityLevel, data_levelEntity, g_assetLevelRefType);
  data_reg_choice_t(g_dataReg, AssetProperty, AssetProperty_Asset, data_asset, g_assetRefType);
  // clang-format on

  g_assetPropertyType = t_AssetProperty;
}
