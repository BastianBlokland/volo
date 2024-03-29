#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"

#include "repo_internal.h"

static const String g_assetRepoQueryResultStrs[] = {
    string_static("RepoQuerySuccess"),
    string_static("RepoQueryErrorNotSupported"),
    string_static("RepoQueryErrorPatternNotSupported"),
    string_static("RepoQueryErrorWhileQuerying"),
};

ASSERT(
    array_elems(g_assetRepoQueryResultStrs) == AssetRepoQueryResult_Count,
    "Incorrect number of AssetRepoQueryResult strings");

String asset_repo_query_result_str(const AssetRepoQueryResult result) {
  diag_assert(result < AssetRepoQueryResult_Count);
  return g_assetRepoQueryResultStrs[result];
}

void asset_repo_destroy(AssetRepo* repo) { repo->destroy(repo); }

bool asset_repo_path(AssetRepo* repo, const String id, DynString* out) {
  if (repo->path) {
    return repo->path(repo, id, out);
  }
  return false;
}

AssetSource* asset_repo_source_open(AssetRepo* repo, const String id) {
  return repo->open(repo, id);
}

bool asset_repo_save(AssetRepo* repo, const String id, const String data) {
  return repo->save && repo->save(repo, id, data);
}

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

AssetRepoQueryResult asset_repo_query(
    AssetRepo*                  repo,
    const String                filterPattern,
    void*                       context,
    const AssetRepoQueryHandler handler) {
  if (repo->query) {
    return repo->query(repo, filterPattern, context, handler);
  }
  return AssetRepoQueryResult_ErrorNotSupported;
}
