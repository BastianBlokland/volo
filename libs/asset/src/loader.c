#include "core_diag.h"

#include "loader_internal.h"

#define asset_loader_name(_NAME_) asset_load_##_NAME_

AssetLoader asset_loader(const AssetFormat format) {
#define RET_LOADER(_NAME_)                                                                         \
  void asset_loader_name(_NAME_)(EcsWorld*, EcsEntityId, AssetSource*);                            \
  return &asset_loader_name(_NAME_)

  switch (format) {
  case AssetFormat_Ftx: {
    RET_LOADER(ftx);
  }
  case AssetFormat_Gra: {
    RET_LOADER(gra);
  }
  case AssetFormat_Obj: {
    RET_LOADER(obj);
  }
  case AssetFormat_Pme: {
    RET_LOADER(pme);
  }
  case AssetFormat_Ppm: {
    RET_LOADER(ppm);
  }
  case AssetFormat_Ptx: {
    RET_LOADER(ptx);
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
  case AssetFormat_Count:
    break;
  }

#undef RET_LOADER
  diag_crash_msg("No asset loader defined for format: {}", fmt_int(format));
}
