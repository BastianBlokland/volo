#pragma once
#include "asset.h"
#include "data.h"

void asset_data_init_arraytex(void);
void asset_data_init_atlas(void);
void asset_data_init_cache(void);
void asset_data_init_decal(void);
void asset_data_init_fonttex(void);
void asset_data_init_graphic(void);
void asset_data_init_icon(void);
void asset_data_init_import_mesh(void);
void asset_data_init_import_texture(void);
void asset_data_init_inputmap(void);
void asset_data_init_level(void);
void asset_data_init_mesh(void);
void asset_data_init_prefab(void);
void asset_data_init_procmesh(void);
void asset_data_init_proctex(void);
void asset_data_init_product(void);
void asset_data_init_script_scene(void);
void asset_data_init_script(void);
void asset_data_init_shader(void);
void asset_data_init_sound(void);
void asset_data_init_terrain(void);
void asset_data_init_tex(void);
void asset_data_init_vfx(void);
void asset_data_init_weapon(void);

extern DataType g_assetRefType;
extern DataType g_assetGeoColor3Type, g_assetGeoColor4Type;
extern DataType g_assetGeoVec2Type, g_assetGeoVec3Type, g_assetGeoVec4Type;
extern DataType g_assetGeoQuatType;
extern DataType g_assetGeoBoxType, g_assetGeoBoxRotatedType;
extern DataType g_assetGeoLineType;
extern DataType g_assetGeoSphereType;
extern DataType g_assetGeoCapsuleType;
extern DataType g_assetGeoMatrixType;
extern DataType g_assetGeoPlaneType;

bool asset_data_patch_refs(EcsWorld*, AssetManagerComp*, DataMeta, Mem data);
