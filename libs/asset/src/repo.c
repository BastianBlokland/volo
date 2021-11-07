#include "core_alloc.h"
#include "core_array.h"

#include "repo_internal.h"

AssetSource* asset_source_open(AssetRepo* repo, String id) { return repo->open(repo, id); }

void asset_source_close(AssetSource* src) {
  if (src->close) {
    src->close(src);
  } else {
    alloc_free_t(g_alloc_heap, src);
  }
}

void asset_repo_destroy(AssetRepo* repo) { repo->destroy(repo); }

String asset_format_str(AssetFormat fmt) {
  static const String names[] = {
      string_static("Raw"),
      string_static("Tga"),
  };
  ASSERT(array_elems(names) == AssetFormat_Count, "Incorrect number of asset-format names");
  return names[fmt];
}

AssetFormat asset_format_from_ext(String ext) {
  (void)ext;
  return AssetFormat_Raw;
}
