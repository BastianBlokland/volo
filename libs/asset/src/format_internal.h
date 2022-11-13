#pragma once
#include "core_string.h"

typedef enum {
  AssetFormat_Atl,
  AssetFormat_Atx,
  AssetFormat_Bin,
  AssetFormat_Bt,
  AssetFormat_Ftx,
  AssetFormat_Gltf,
  AssetFormat_Gra,
  AssetFormat_Imp,
  AssetFormat_Obj,
  AssetFormat_Pme,
  AssetFormat_Ppm,
  AssetFormat_Ptx,
  AssetFormat_R32,
  AssetFormat_Raw,
  AssetFormat_Spv,
  AssetFormat_Tga,
  AssetFormat_Ttf,
  AssetFormat_Vfx,
  AssetFormat_Wea,

  AssetFormat_Count,
} AssetFormat;

String      asset_format_str(AssetFormat);
AssetFormat asset_format_from_ext(String ext);
