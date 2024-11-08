#include "core_bits.h"
#include "core_diag.h"

#include "loader_internal.h"

// clang-format off

#define ASSET_FOREACH_LOADER(_X_)                                                                  \
  _X_(AssetFormat_Decal,            decal,              1  )                                       \
  _X_(AssetFormat_FontTtf,          font_ttf,           1  )                                       \
  _X_(AssetFormat_Graphic,          graphic,            1  )                                       \
  _X_(AssetFormat_Icon,             icon,               1  )                                       \
  _X_(AssetFormat_IconBin,          icon_bin,           1  )                                       \
  _X_(AssetFormat_Inputs,           inputs,             1  )                                       \
  _X_(AssetFormat_Level,            level,              1  )                                       \
  _X_(AssetFormat_LevelBin,         level,              1  )                                       \
  _X_(AssetFormat_MeshBin,          mesh_bin,           1  )                                       \
  _X_(AssetFormat_MeshGlb,          mesh_glb,           1  )                                       \
  _X_(AssetFormat_MeshGltf,         mesh_gltf,          1  )                                       \
  _X_(AssetFormat_MeshObj,          mesh_obj,           1  )                                       \
  _X_(AssetFormat_MeshProc,         mesh_proc,          1  )                                       \
  _X_(AssetFormat_Prefabs,          prefabs,            1  )                                       \
  _X_(AssetFormat_Products,         products,           1  )                                       \
  _X_(AssetFormat_Raw,              raw,                1  )                                       \
  _X_(AssetFormat_Script,           script,             1  )                                       \
  _X_(AssetFormat_ScriptBin,        script_bin,         1  )                                       \
  _X_(AssetFormat_ShaderBin,        shader_bin,         1  )                                       \
  _X_(AssetFormat_ShaderGlslFrag,   shader_glsl_frag,   1  )                                       \
  _X_(AssetFormat_ShaderGlslVert,   shader_glsl_vert,   1  )                                       \
  _X_(AssetFormat_ShaderSpv,        shader_spv,         1  )                                       \
  _X_(AssetFormat_SoundBin,         sound_bin,          1  )                                       \
  _X_(AssetFormat_SoundWav,         sound_wav,          1  )                                       \
  _X_(AssetFormat_Terrain,          terrain,            1  )                                       \
  _X_(AssetFormat_TexArray,         tex_array,          1  )                                       \
  _X_(AssetFormat_TexAtlas,         tex_atlas,          1  )                                       \
  _X_(AssetFormat_TexAtlasBin,      tex_atlas_bin,      1  )                                       \
  _X_(AssetFormat_TexBin,           tex_bin,            1  )                                       \
  _X_(AssetFormat_TexFont,          tex_font,           1  )                                       \
  _X_(AssetFormat_TexFontBin,       tex_font_bin,       1  )                                       \
  _X_(AssetFormat_TexHeight16,      tex_height16,       1  )                                       \
  _X_(AssetFormat_TexHeight32,      tex_height32,       1  )                                       \
  _X_(AssetFormat_TexPng,           tex_png,            1  )                                       \
  _X_(AssetFormat_TexPpm,           tex_ppm,            1  )                                       \
  _X_(AssetFormat_TexProc,          tex_proc,           1  )                                       \
  _X_(AssetFormat_TexTga,           tex_tga,            1  )                                       \
  _X_(AssetFormat_Vfx,              vfx,                1  )                                       \
  _X_(AssetFormat_Weapons,          weapons,            1  )

#define ASSET_LOADER_ITR(_FORMAT_, _NAME_, _VERSION_) void asset_load_##_NAME_(EcsWorld*, String, EcsEntityId, AssetSource*);
ASSET_FOREACH_LOADER(ASSET_LOADER_ITR)
#undef ASSET_LOADER_ITR

static const AssetLoader g_assetLoaders[AssetFormat_Count] = {
#define ASSET_LOADER_ITR(_FORMAT_, _NAME_, _VERSION_) [_FORMAT_] = &asset_load_##_NAME_,
    ASSET_FOREACH_LOADER(ASSET_LOADER_ITR)
};
#undef ASSET_LOADER_ITR

static const u32 g_assetLoaderVersions[AssetFormat_Count] = {
#define ASSET_LOADER_ITR(_FORMAT_, _NAME_, _VERSION_) [_FORMAT_] = _VERSION_,
    ASSET_FOREACH_LOADER(ASSET_LOADER_ITR)
};
#undef ASSET_LOADER_ITR

#undef ASSET_FOREACH_LOADER

// clang-format on

AssetLoader asset_loader(const AssetFormat format) { return g_assetLoaders[format]; }
u32         asset_loader_version(const AssetFormat format) { return g_assetLoaderVersions[format]; }

u32 asset_loader_hash(const AssetFormat format) {
  const u32 version = g_assetLoaderVersions[format];
  return bits_hash_32_val(version);
}
