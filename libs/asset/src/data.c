#include "core_types.h"

#include "data_internal.h"

static bool g_initalized;

void asset_data_init(void) {
  if (!g_initalized) {
    g_initalized = true;

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
    asset_data_init_terrain();
    asset_data_init_vfx();
    asset_data_init_weapon();
  }
}
