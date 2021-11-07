#include "core_diag.h"

#include "loader_internal.h"

#define asset_loader_name(_NAME_) asset_load_##_NAME_

AssetLoader asset_loader(const AssetFormat format) {
#define RET_LOADER(_NAME_)                                                                         \
  void asset_loader_name(_NAME_)(EcsWorld*, EcsEntityId, AssetSource*);                            \
  return &asset_loader_name(_NAME_)

  switch (format) {
  case AssetFormat_Raw: {
    RET_LOADER(raw);
  }
  case AssetFormat_Tga: {
    RET_LOADER(tga);
  }
  case AssetFormat_Count:
    break;
  }

#undef RET_LOADER
  diag_crash_msg("No asset loader defined for format: {}", fmt_int(format));
}
