#include "core_diag.h"

#include "loader_internal.h"

// clang-format off

#define ASSET_FOREACH_LOADER(_X_)                                                                  \
  _X_(AssetFormat_ArrayTex,   arraytex           )                                                 \
  _X_(AssetFormat_Atlas,      atlas              )                                                 \
  _X_(AssetFormat_BinTex,     tex_bin            )                                                 \
  _X_(AssetFormat_Cursor,     cursor             )                                                 \
  _X_(AssetFormat_Decal,      decal              )                                                 \
  _X_(AssetFormat_FontTex,    fonttex            )                                                 \
  _X_(AssetFormat_GlslFrag,   glsl_frag          )                                                 \
  _X_(AssetFormat_GlslVert,   glsl_vert          )                                                 \
  _X_(AssetFormat_Gltf,       gltf               )                                                 \
  _X_(AssetFormat_Graphic,    graphic            )                                                 \
  _X_(AssetFormat_Height16,   height16           )                                                 \
  _X_(AssetFormat_Height32,   height32           )                                                 \
  _X_(AssetFormat_Inputs,     inputs             )                                                 \
  _X_(AssetFormat_Level,      level              )                                                 \
  _X_(AssetFormat_Obj,        obj                )                                                 \
  _X_(AssetFormat_Ppm,        ppm                )                                                 \
  _X_(AssetFormat_Prefabs,    prefabs            )                                                 \
  _X_(AssetFormat_ProcMesh,   procmesh           )                                                 \
  _X_(AssetFormat_ProcTex,    proctex            )                                                 \
  _X_(AssetFormat_Products,   products           )                                                 \
  _X_(AssetFormat_Raw,        raw                )                                                 \
  _X_(AssetFormat_Script,     script             )                                                 \
  _X_(AssetFormat_Spv,        spv                )                                                 \
  _X_(AssetFormat_Terrain,    terrain            )                                                 \
  _X_(AssetFormat_Tga,        tga                )                                                 \
  _X_(AssetFormat_Ttf,        ttf                )                                                 \
  _X_(AssetFormat_Vfx,        vfx                )                                                 \
  _X_(AssetFormat_Wav,        wav                )                                                 \
  _X_(AssetFormat_Weapons,    weapons            )

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
