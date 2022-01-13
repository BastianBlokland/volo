#include "asset_font.h"
#include "core_alloc.h"
#include "core_array.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "repo_internal.h"

typedef enum {
  TtfError_None = 0,
  TtfError_Malformed,
  TtfError_NotSupported,

  TtfError_Count,
} TtfError;

static String ttf_error_str(TtfError res) {
  static const String msgs[] = {
      string_static("None"),
      string_static("Malformed TrueType font-data"),
      string_static("Unsupported TrueType font-data"),
  };
  ASSERT(array_elems(msgs) == TtfError_Count, "Incorrect number of ttf-error messages");
  return msgs[res];
}

static void ttf_load_fail(EcsWorld* world, const EcsEntityId assetEntity, const TtfError err) {
  log_e("Failed to parse TrueType font", log_param("error", fmt_text(ttf_error_str(err))));
  ecs_world_add_empty_t(world, assetEntity, AssetFailedComp);
}

void asset_load_ttf(EcsWorld* world, EcsEntityId assetEntity, AssetSource* src) {
  const TtfError err = TtfError_NotSupported;
  ttf_load_fail(world, assetEntity, err);
  asset_repo_source_close(src);
}
