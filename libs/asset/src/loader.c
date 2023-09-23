#include "core_diag.h"

#include "loader_internal.h"

#define asset_loader_name(_NAME_) asset_load_##_NAME_

AssetLoader asset_loader(const AssetFormat format) {
#define RET_LOADER(_NAME_)                                                                         \
  void asset_loader_name(_NAME_)(EcsWorld*, String, EcsEntityId, AssetSource*);                    \
  return &asset_loader_name(_NAME_)

  switch (format) {
  case AssetFormat_Atl: {
    RET_LOADER(atl);
  }
  case AssetFormat_Atx: {
    RET_LOADER(atx);
  }
  case AssetFormat_Bin: {
    RET_LOADER(raw);
  }
  case AssetFormat_Bt: {
    RET_LOADER(bt);
  }
  case AssetFormat_Dcl: {
    RET_LOADER(dcl);
  }
  case AssetFormat_Ftx: {
    RET_LOADER(ftx);
  }
  case AssetFormat_Gltf: {
    RET_LOADER(gltf);
  }
  case AssetFormat_Graphic: {
    RET_LOADER(graphic);
  }
  case AssetFormat_Imp: {
    RET_LOADER(imp);
  }
  case AssetFormat_Level: {
    RET_LOADER(level);
  }
  case AssetFormat_Obj: {
    RET_LOADER(obj);
  }
  case AssetFormat_Pfb: {
    RET_LOADER(pfb);
  }
  case AssetFormat_Pme: {
    RET_LOADER(pme);
  }
  case AssetFormat_Ppm: {
    RET_LOADER(ppm);
  }
  case AssetFormat_Pro: {
    RET_LOADER(pro);
  }
  case AssetFormat_Ptx: {
    RET_LOADER(ptx);
  }
  case AssetFormat_R16: {
    RET_LOADER(r16);
  }
  case AssetFormat_R32: {
    RET_LOADER(r32);
  }
  case AssetFormat_Raw: {
    RET_LOADER(raw);
  }
  case AssetFormat_Spv: {
    RET_LOADER(spv);
  }
  case AssetFormat_Tga: {
    RET_LOADER(tga);
  }
  case AssetFormat_Ttf: {
    RET_LOADER(ttf);
  }
  case AssetFormat_Vfx: {
    RET_LOADER(vfx);
  }
  case AssetFormat_Wav: {
    RET_LOADER(wav);
  }
  case AssetFormat_Wea: {
    RET_LOADER(wea);
  }
  case AssetFormat_Count:
    break;
  }

#undef RET_LOADER
  diag_crash_msg("No asset loader defined for format: {}", fmt_int(format));
}
