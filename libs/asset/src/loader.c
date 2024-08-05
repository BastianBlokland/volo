#include "core_diag.h"

#include "loader_internal.h"

// clang-format off

#define ASSET_FOREACH_LOADER(_X_)                                                                  \
  _X_(AssetFormat_Cursor,           cursor             )                                           \
  _X_(AssetFormat_Decal,            decal              )                                           \
  _X_(AssetFormat_FontTex,          fonttex            )                                           \
  _X_(AssetFormat_FontTtf,          font_ttf           )                                           \
  _X_(AssetFormat_Graphic,          graphic            )                                           \
  _X_(AssetFormat_Inputs,           inputs             )                                           \
  _X_(AssetFormat_Level,            level              )                                           \
  _X_(AssetFormat_MeshGltf,         mesh_gltf          )                                           \
  _X_(AssetFormat_MeshObj,          mesh_obj           )                                           \
  _X_(AssetFormat_MeshProc,         mesh_proc          )                                           \
  _X_(AssetFormat_Prefabs,          prefabs            )                                           \
  _X_(AssetFormat_Products,         products           )                                           \
  _X_(AssetFormat_Raw,              raw                )                                           \
  _X_(AssetFormat_Script,           script             )                                           \
  _X_(AssetFormat_ShaderBin,        shader_bin         )                                           \
  _X_(AssetFormat_ShaderGlslFrag,   shader_glsl_frag   )                                           \
  _X_(AssetFormat_ShaderGlslVert,   shader_glsl_vert   )                                           \
  _X_(AssetFormat_ShaderSpv,        shader_spv         )                                           \
  _X_(AssetFormat_SoundWav,         sound_wav          )                                           \
  _X_(AssetFormat_Terrain,          terrain            )                                           \
  _X_(AssetFormat_TexArray,         tex_array          )                                           \
  _X_(AssetFormat_TexAtlas,         tex_atlas          )                                           \
  _X_(AssetFormat_TexBin,           tex_bin            )                                           \
  _X_(AssetFormat_TexHeight16,      tex_height16       )                                           \
  _X_(AssetFormat_TexHeight32,      tex_height32       )                                           \
  _X_(AssetFormat_TexPpm,           tex_ppm            )                                           \
  _X_(AssetFormat_TexProc,          tex_proc           )                                           \
  _X_(AssetFormat_TexTga,           tex_tga            )                                           \
  _X_(AssetFormat_Vfx,              vfx                )                                           \
  _X_(AssetFormat_Weapons,          weapons            )

#define ASSET_LOADER_DECLARE(_FORMAT_, _NAME_) void asset_load_##_NAME_(EcsWorld*, String, EcsEntityId, AssetSource*);
ASSET_FOREACH_LOADER(ASSET_LOADER_DECLARE)
#undef ASSET_LOADER_DECLARE

static const AssetLoader g_assetLoaders[AssetFormat_Count] = {
#define ASSET_LOADER_REGISTER(_FORMAT_, _NAME_) [_FORMAT_] = &asset_load_##_NAME_,
    ASSET_FOREACH_LOADER(ASSET_LOADER_REGISTER)
};
#undef ASSET_LOADER_REGISTER

#undef ASSET_FOREACH_LOADER

// clang-format on

AssetLoader asset_loader(const AssetFormat format) { return g_assetLoaders[format]; }
