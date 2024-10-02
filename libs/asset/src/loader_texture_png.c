#include "core_array.h"
#include "ecs_world.h"
#include "log_logger.h"

#include "repo_internal.h"

/**
 * Portable Network Graphics.
 *
 * Spec: https://www.w3.org/TR/png-3/
 */

static const String g_pngMagic = string_lit("\x89\x50\x4E\x47\x0D\x0A\x1A\x0A");

typedef enum {
  PngError_None = 0,
  PngError_Truncated,
  PngError_Malformed,

  PngError_Count,
} PngError;

static String png_error_str(const PngError err) {
  static const String g_msgs[] = {
      string_static("None"),
      string_static("Truncated png data"),
      string_static("Malformed png data"),
  };
  ASSERT(array_elems(g_msgs) == PngError_Count, "Incorrect number of png-error messages");
  return g_msgs[err];
}

static void png_load_fail(EcsWorld* w, const EcsEntityId e, const String id, const PngError err) {
  log_e(
      "Failed to parse Png texture",
      log_param("id", fmt_text(id)),
      log_param("entity", ecs_entity_fmt(e)),
      log_param("error", fmt_text(png_error_str(err))));
  ecs_world_add_empty_t(w, e, AssetFailedComp);
}

void asset_load_tex_png(
    EcsWorld* world, const String id, const EcsEntityId entity, AssetSource* src) {

  Mem data = src->data;
  if (UNLIKELY(data.size < g_pngMagic.size)) {
    png_load_fail(world, entity, id, PngError_Truncated);
    goto Ret;
  }
  if (UNLIKELY(!string_starts_with(data, g_pngMagic))) {
    png_load_fail(world, entity, id, PngError_Malformed);
    goto Ret;
  }

  // TODO: Implement png parsing.
  png_load_fail(world, entity, id, PngError_Malformed);

Ret:
  asset_repo_source_close(src);
}
