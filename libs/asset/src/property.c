#include "asset_property.h"
#include "data_registry.h"

#include "data_internal.h"

DataType g_assetPropertyType;

void asset_data_init_property(void) {
  // clang-format off
  data_reg_union_t(g_dataReg, AssetProperty, type);
  data_reg_union_name_t(g_dataReg, AssetProperty, name, DataUnionNameType_StringHash);
  data_reg_choice_t(g_dataReg, AssetProperty, AssetPropertyType_Num, data_num, data_prim_t(f64));
  data_reg_choice_t(g_dataReg, AssetProperty, AssetPropertyType_Bool, data_bool, data_prim_t(bool));
  data_reg_choice_t(g_dataReg, AssetProperty, AssetPropertyType_Vec3, data_vec3, g_assetGeoVec3Type);
  data_reg_choice_t(g_dataReg, AssetProperty, AssetPropertyType_Quat, data_quat, g_assetGeoQuatType);
  data_reg_choice_t(g_dataReg, AssetProperty, AssetPropertyType_Color, data_color, g_assetGeoColor4Type);
  // clang-format on

  g_assetPropertyType = t_AssetProperty;
}
