#pragma once
#include "core_string.h"

typedef enum {
  AssetFormat_Graphic,
  AssetFormat_Obj,
  AssetFormat_Ppm,
  AssetFormat_Raw,
  AssetFormat_Spv,
  AssetFormat_Tga,
  AssetFormat_Ttf,

  AssetFormat_Count,
} AssetFormat;

String      asset_format_str(AssetFormat);
AssetFormat asset_format_from_ext(String ext);
