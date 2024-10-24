#pragma once
#include "core_string.h"
#include "data_registry.h"

typedef enum {
  AssetFormat_Decal,
  AssetFormat_FontTtf,
  AssetFormat_Graphic,
  AssetFormat_Icon,
  AssetFormat_IconBin,
  AssetFormat_Inputs,
  AssetFormat_Level,
  AssetFormat_LevelBin,
  AssetFormat_MeshBin,
  AssetFormat_MeshGlb,
  AssetFormat_MeshGltf,
  AssetFormat_MeshObj,
  AssetFormat_MeshProc,
  AssetFormat_Prefabs,
  AssetFormat_Products,
  AssetFormat_Raw,
  AssetFormat_Script,
  AssetFormat_ScriptBin,
  AssetFormat_ShaderBin,
  AssetFormat_ShaderGlsl,
  AssetFormat_ShaderGlslFrag,
  AssetFormat_ShaderGlslVert,
  AssetFormat_ShaderSpv,
  AssetFormat_SoundBin,
  AssetFormat_SoundWav,
  AssetFormat_Terrain,
  AssetFormat_TexArray,
  AssetFormat_TexAtlas,
  AssetFormat_TexAtlasBin,
  AssetFormat_TexBin,
  AssetFormat_TexFont,
  AssetFormat_TexFontBin,
  AssetFormat_TexHeight16,
  AssetFormat_TexHeight32,
  AssetFormat_TexPng,
  AssetFormat_TexPpm,
  AssetFormat_TexProc,
  AssetFormat_TexTga,
  AssetFormat_Vfx,
  AssetFormat_Weapons,

  AssetFormat_Count,
} AssetFormat;

String      asset_format_str(AssetFormat);
AssetFormat asset_format_from_ext(String ext);
AssetFormat asset_format_from_bin_meta(DataMeta);
