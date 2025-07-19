#include "core_alloc.h"
#include "core_dynarray.h"
#include "core_path.h"
#include "core_time.h"
#include "log_logger.h"

#include "repo_internal.h"

typedef struct {
  StringHash idHash;
  String     id;
  String     data;
} RepoEntry;

typedef struct {
  AssetRepo api;
  TimeReal  createTime;
  DynArray  entries; // RepoEntry[], sorted on idHash
} AssetRepoMem;

static i8 asset_compare_entry(const void* a, const void* b) {
  return compare_stringhash(field_ptr(a, RepoEntry, idHash), field_ptr(b, RepoEntry, idHash));
}

static bool asset_source_mem_stat(
    AssetRepo* repo, const String id, const AssetRepoLoaderHasher loaderHasher, AssetInfo* out) {
  (void)loaderHasher;
  AssetRepoMem* repoMem = (AssetRepoMem*)repo;

  const AssetFormat fmt    = asset_format_from_ext(path_extension(id));
  const StringHash  idHash = string_hash(id);
  const RepoEntry*  entry  = dynarray_search_binary(
      &repoMem->entries, asset_compare_entry, mem_struct(RepoEntry, .idHash = idHash).ptr);

  if (!entry) {
    return false;
  }

  *out = (AssetInfo){
      .format  = fmt,
      .flags   = AssetInfoFlags_None,
      .size    = entry->data.size,
      .modTime = repoMem->createTime,
  };
  return true;
}

static AssetSource*
asset_source_mem_open(AssetRepo* repo, const String id, const AssetRepoLoaderHasher loaderHasher) {
  (void)loaderHasher;
  AssetRepoMem* repoMem = (AssetRepoMem*)repo;

  const AssetFormat fmt    = asset_format_from_ext(path_extension(id));
  const StringHash  idHash = string_hash(id);
  const RepoEntry*  entry  = dynarray_search_binary(
      &repoMem->entries, asset_compare_entry, mem_struct(RepoEntry, .idHash = idHash).ptr);

  if (!entry) {
    log_w("Failed to find entry", log_param("id", fmt_path(id)));
    return null;
  }

  AssetSource* src = alloc_alloc_t(g_allocHeap, AssetSource);

  *src = (AssetSource){
      .data    = entry->data,
      .format  = fmt,
      .flags   = AssetInfoFlags_None,
      .modTime = repoMem->createTime,
  };

  return src;
}

static AssetRepoQueryResult asset_repo_mem_query(
    AssetRepo* repo, const String pattern, void* ctx, const AssetRepoQueryHandler handler) {
  AssetRepoMem* repoMem = (AssetRepoMem*)repo;

  dynarray_for_t(&repoMem->entries, RepoEntry, entry) {
    if (string_match_glob(entry->id, pattern, StringMatchFlags_None)) {
      handler(ctx, entry->id);
    }
  }

  return AssetRepoQueryResult_Success;
}

static void asset_repo_mem_destroy(AssetRepo* repo) {
  AssetRepoMem* repoMem = (AssetRepoMem*)repo;

  dynarray_for_t(&repoMem->entries, RepoEntry, entry) {
    string_free(g_allocHeap, entry->id);
    string_maybe_free(g_allocHeap, entry->data);
  };
  dynarray_destroy(&repoMem->entries);

  alloc_free_t(g_allocHeap, repoMem);
}

AssetRepo* asset_repo_create_mem(const AssetMemRecord* records, const usize recordCount) {
  AssetRepoMem* repo = alloc_alloc_t(g_allocHeap, AssetRepoMem);

  *repo = (AssetRepoMem){
      .api =
          {
              .stat    = asset_source_mem_stat,
              .open    = asset_source_mem_open,
              .destroy = asset_repo_mem_destroy,
              .query   = asset_repo_mem_query,
          },
      .createTime = time_real_clock(),
      .entries    = dynarray_create_t(g_allocHeap, RepoEntry, recordCount),
  };

  for (usize i = 0; i != recordCount; ++i) {
    RepoEntry entry = {
        .idHash = string_hash(records[i].id),
        .id     = string_dup(g_allocHeap, records[i].id),
        .data   = string_maybe_dup(g_allocHeap, records[i].data),
    };
    *dynarray_insert_sorted_t(&repo->entries, RepoEntry, asset_compare_entry, &entry) = entry;
  }
  return (AssetRepo*)repo;
}
