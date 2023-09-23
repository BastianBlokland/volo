#pragma once
#include "core_string.h"

typedef enum {
  AssetFormat_Atlas,
  AssetFormat_ArrayTex,
  AssetFormat_Bin,
  AssetFormat_Bt,
  AssetFormat_Decal,
  AssetFormat_Ftx,
  AssetFormat_Gltf,
  AssetFormat_Graphic,
  AssetFormat_Inputs,
  AssetFormat_Level,
  AssetFormat_Obj,
  AssetFormat_Prefabs,
  AssetFormat_ProcMesh,
  AssetFormat_Ppm,
  AssetFormat_Products,
  AssetFormat_ProcTex,
  AssetFormat_R16,
  AssetFormat_R32,
  AssetFormat_Raw,
  AssetFormat_Spv,
  AssetFormat_Tga,
  AssetFormat_Ttf,
  AssetFormat_Vfx,
  AssetFormat_Wav,
  AssetFormat_Weapons,

  AssetFormat_Count,
} AssetFormat;

String      asset_format_str(AssetFormat);
AssetFormat asset_format_from_ext(String ext);
