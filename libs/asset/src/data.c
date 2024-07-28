#include "core_thread.h"
#include "core_types.h"

#include "data_internal.h"

DataType g_assetColorType;

static void asset_data_init_types() {
  data_reg_struct_t(g_dataReg, AssetColor);
  data_reg_field_t(g_dataReg, AssetColor, r, data_prim_t(f32));
  data_reg_field_t(g_dataReg, AssetColor, g, data_prim_t(f32));
  data_reg_field_t(g_dataReg, AssetColor, b, data_prim_t(f32));
  data_reg_field_t(g_dataReg, AssetColor, a, data_prim_t(f32));
  data_reg_comment_t(g_dataReg, AssetColor, "HDR Color definition (components default to 0)");

  g_assetColorType = t_AssetColor;
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
    asset_data_init_vfx();
    asset_data_init_weapon();

    g_init = true;
  }
  thread_spinlock_unlock(&g_initLock);
}
