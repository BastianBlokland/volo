#include "asset_atlas.h"
#include "asset_fonttex.h"
#include "asset_icon.h"
#include "asset_level.h"
#include "asset_mesh.h"
#include "asset_shader.h"
#include "asset_sound.h"
#include "asset_texture.h"
#include "core_diag.h"

#include "format_internal.h"

static const String g_assetFormatExtensions[AssetFormat_Count] = {
    [AssetFormat_Decal]          = string_static("decal"),
    [AssetFormat_FontTtf]        = string_static("ttf"),
    [AssetFormat_Graphic]        = string_static("graphic"),
    [AssetFormat_Icon]           = string_static("icon"),
    [AssetFormat_Inputs]         = string_static("inputs"),
    [AssetFormat_Level]          = string_static("level"),
    [AssetFormat_MeshGltf]       = string_static("gltf"),
    [AssetFormat_MeshObj]        = string_static("obj"),
    [AssetFormat_MeshProc]       = string_static("procmesh"),
    [AssetFormat_Prefabs]        = string_static("prefabs"),
    [AssetFormat_Products]       = string_static("products"),
    [AssetFormat_Script]         = string_static("script"),
    [AssetFormat_ShaderGlsl]     = string_static("glsl"),
    [AssetFormat_ShaderGlslFrag] = string_static("frag"),
    [AssetFormat_ShaderGlslVert] = string_static("vert"),
    [AssetFormat_ShaderSpv]      = string_static("spv"),
    [AssetFormat_SoundWav]       = string_static("wav"),
    [AssetFormat_Terrain]        = string_static("terrain"),
    [AssetFormat_TexArray]       = string_static("arraytex"),
    [AssetFormat_TexAtlas]       = string_static("atlas"),
    [AssetFormat_TexFont]        = string_static("fonttex"),
    [AssetFormat_TexHeight16]    = string_static("r16"),
    [AssetFormat_TexHeight32]    = string_static("r32"),
    [AssetFormat_TexPpm]         = string_static("ppm"),
    [AssetFormat_TexProc]        = string_static("proctex"),
    [AssetFormat_TexTga]         = string_static("tga"),
    [AssetFormat_Vfx]            = string_static("vfx"),
    [AssetFormat_Weapons]        = string_static("weapons"),
};

static const String g_assetFormatNames[AssetFormat_Count] = {
    [AssetFormat_Decal]          = string_static("Decal"),
    [AssetFormat_FontTtf]        = string_static("FontTtf"),
    [AssetFormat_Graphic]        = string_static("Graphic"),
    [AssetFormat_Icon]           = string_static("Icon"),
    [AssetFormat_IconBin]        = string_static("IconBin"),
    [AssetFormat_Inputs]         = string_static("Inputs"),
    [AssetFormat_Level]          = string_static("Level"),
    [AssetFormat_LevelBin]       = string_static("LevelBin"),
    [AssetFormat_MeshBin]        = string_static("MeshBin"),
    [AssetFormat_MeshGlb]        = string_static("MeshGlb"),
    [AssetFormat_MeshGltf]       = string_static("MeshGltf"),
    [AssetFormat_MeshObj]        = string_static("MeshObj"),
    [AssetFormat_MeshProc]       = string_static("MeshProc"),
    [AssetFormat_Prefabs]        = string_static("Prefabs"),
    [AssetFormat_Products]       = string_static("Products"),
    [AssetFormat_Raw]            = string_static("Raw"),
    [AssetFormat_Script]         = string_static("Script"),
    [AssetFormat_ShaderBin]      = string_static("ShaderBin"),
    [AssetFormat_ShaderGlsl]     = string_static("ShaderGlsl"),
    [AssetFormat_ShaderGlslFrag] = string_static("ShaderGlslFrag"),
    [AssetFormat_ShaderGlslVert] = string_static("ShaderGlslVert"),
    [AssetFormat_ShaderSpv]      = string_static("ShaderSpv"),
    [AssetFormat_SoundBin]       = string_static("SoundBin"),
    [AssetFormat_SoundWav]       = string_static("SoundWav"),
    [AssetFormat_Terrain]        = string_static("Terrain"),
    [AssetFormat_TexArray]       = string_static("TexArray"),
    [AssetFormat_TexAtlas]       = string_static("TexAtlas"),
    [AssetFormat_TexAtlasBin]    = string_static("TexAtlasBin"),
    [AssetFormat_TexBin]         = string_static("TexBin"),
    [AssetFormat_TexFont]        = string_static("TexFont"),
    [AssetFormat_TexFontBin]     = string_static("TexFontBin"),
    [AssetFormat_TexHeight16]    = string_static("TexHeight16"),
    [AssetFormat_TexHeight32]    = string_static("TexHeight32"),
    [AssetFormat_TexPpm]         = string_static("TexPpm"),
    [AssetFormat_TexProc]        = string_static("TexProc"),
    [AssetFormat_TexTga]         = string_static("TexTga"),
    [AssetFormat_Vfx]            = string_static("Vfx"),
    [AssetFormat_Weapons]        = string_static("Weapons"),
};

static const DataMeta* g_assetFormatBinMeta[AssetFormat_Count] = {
    [AssetFormat_IconBin]     = &g_assetIconMeta,
    [AssetFormat_LevelBin]    = &g_assetLevelDefMeta,
    [AssetFormat_MeshBin]     = &g_assetMeshBundleMeta,
    [AssetFormat_ShaderBin]   = &g_assetShaderMeta,
    [AssetFormat_SoundBin]    = &g_assetSoundMeta,
    [AssetFormat_TexAtlasBin] = &g_assetAtlasBundleMeta,
    [AssetFormat_TexBin]      = &g_assetTexMeta,
    [AssetFormat_TexFontBin]  = &g_assetFontTexBundleMeta,
};

String asset_format_str(const AssetFormat fmt) {
  diag_assert(fmt < AssetFormat_Count);
  return g_assetFormatNames[fmt];
}

AssetFormat asset_format_from_ext(const String ext) {
  for (AssetFormat fmt = 0; fmt != AssetFormat_Count; ++fmt) {
    if (string_eq(ext, g_assetFormatExtensions[fmt])) {
      return fmt;
    }
  }
  return AssetFormat_Raw;
}

AssetFormat asset_format_from_bin_meta(const DataMeta meta) {
  for (AssetFormat fmt = 0; fmt != AssetFormat_Count; ++fmt) {
    if (g_assetFormatBinMeta[fmt] && meta.data == g_assetFormatBinMeta[fmt]->data) {
      return fmt;
    }
  }
  return AssetFormat_Raw;
}
