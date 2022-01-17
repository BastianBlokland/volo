#include "asset_texture.h"
#include "core_array.h"
#include "ecs_world.h"

#include "repo_internal.h"

typedef enum {
  FtxError_None = 0,
  FtxError_Malformed,

  FtxError_Count,
} FtxError;

// static String Ftx_error_str(const FtxError err) {
//   static const String msgs[] = {
//       string_static("None"),
//       string_static("Malformed ftx definition"),
//   };
//   ASSERT(array_elems(msgs) == FtxError_Count, "Incorrect number of ftx-error messages");
//   return msgs[err];
// }

void asset_load_ftx(EcsWorld* world, EcsEntityId assetEntity, AssetSource* src) {
  String   input = src->data;
  FtxError err   = FtxError_None;

  (void)input;
  (void)err;

  ecs_world_add_empty_t(world, assetEntity, AssetLoadedComp);
  asset_repo_source_close(src);
}
