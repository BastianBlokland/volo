#include "core_alloc.h"

#include "repo_internal.h"

void asset_repo_destroy(AssetRepo* repo) { repo->destroy(repo); }

AssetSource* asset_repo_source_open(AssetRepo* repo, String id) { return repo->open(repo, id); }

void asset_repo_source_close(AssetSource* src) {
  if (src->close) {
    src->close(src);
  } else {
    alloc_free_t(g_alloc_heap, src);
  }
}

void asset_repo_changes_watch(AssetRepo* repo, const String id, const u64 userData) {
  if (repo->changesWatch) {
    repo->changesWatch(repo, id, userData);
  }
}

bool asset_repo_changes_poll(AssetRepo* repo, u64* outUserData) {
  if (repo->changesPoll) {
    return repo->changesPoll(repo, outUserData);
  }
  return false;
}
