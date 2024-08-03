#include "core_thread.h"
#include "core_types.h"
#include "geo_color.h"
#include "geo_quat.h"
#include "geo_vector.h"

#include "data_internal.h"

DataType g_assetGeoColorType;
DataType g_assetGeoVec3Type;
DataType g_assetGeoQuatType;

static void asset_data_init_types(void) {
  data_reg_struct_t(g_dataReg, GeoColor);
  data_reg_field_t(g_dataReg, GeoColor, r, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, GeoColor, g, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, GeoColor, b, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, GeoColor, a, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_comment_t(g_dataReg, GeoColor, "HDR Color");

  data_reg_struct_t(g_dataReg, GeoVector);
  data_reg_field_t(g_dataReg, GeoVector, x, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, GeoVector, y, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, GeoVector, z, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_comment_t(g_dataReg, GeoVector, "3D Vector");

  data_reg_struct_t(g_dataReg, GeoQuat);
  data_reg_field_t(g_dataReg, GeoQuat, x, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, GeoQuat, y, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, GeoQuat, z, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_field_t(g_dataReg, GeoQuat, w, data_prim_t(f32), .flags = DataFlags_Opt);
  data_reg_comment_t(g_dataReg, GeoQuat, "Quaternion");

  g_assetGeoColorType = t_GeoColor;
  g_assetGeoVec3Type  = t_GeoVector;
  g_assetGeoQuatType  = t_GeoQuat;
}

void asset_data_init(void) {
  static bool           g_init;
  static ThreadSpinLock g_initLock;
  if (g_init) {
    return;
  }
  thread_spinlock_lock(&g_initLock);
  if (!g_init) {
    asset_data_init_types();

    asset_data_init_arraytex();
    asset_data_init_atlas();
    asset_data_init_cursor();
    asset_data_init_decal();
    asset_data_init_fonttex();
    asset_data_init_graphic();
    asset_data_init_inputmap();
    asset_data_init_level();
    asset_data_init_prefab();
    asset_data_init_procmesh();
    asset_data_init_proctex();
    asset_data_init_product();
    asset_data_init_script();
    asset_data_init_terrain();
    asset_data_init_tex();
    asset_data_init_vfx();
    asset_data_init_weapon();

    g_init = true;
  }
  thread_spinlock_unlock(&g_initLock);
}
