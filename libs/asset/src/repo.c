#include "core_alloc.h"

#include "repo_internal.h"

AssetSource* asset_source_open(AssetRepo* repo, String id) { return repo->open(repo, id); }

void asset_source_close(AssetSource* src) {
  if (src->close) {
    src->close(src);
  } else {
    alloc_free_t(g_alloc_heap, src);
  }
}

AssetFormat asset_format_from_ext(String ext) {
  if (string_eq(ext, string_lit("tga"))) {
    return AssetFormat_Tga;
  }
  return AssetFormat_Raw;
}
