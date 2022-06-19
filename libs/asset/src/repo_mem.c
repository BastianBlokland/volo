#include "core_alloc.h"
#include "core_dynarray.h"
#include "core_path.h"
#include "log_logger.h"

#include "repo_internal.h"

typedef struct {
  StringHash idHash;
  String     data;
} RepoEntry;

typedef struct {
  AssetRepo api;
  DynArray  entries; // RepoEntry[], sorted on idHash
} AssetRepoMem;

static i8 asset_compare_entry(const void* a, const void* b) {
  return compare_stringhash(field_ptr(a, RepoEntry, idHash), field_ptr(b, RepoEntry, idHash));
}

static AssetSource* asset_source_mem_open(AssetRepo* repo, const String id) {
  AssetRepoMem* repoMem = (AssetRepoMem*)repo;

  const AssetFormat fmt    = asset_format_from_ext(path_extension(id));
  const StringHash  idHash = string_hash(id);
  const RepoEntry*  entry  = dynarray_search_binary(
      &repoMem->entries, asset_compare_entry, mem_struct(RepoEntry, .idHash = idHash).ptr);

  if (!entry) {
    log_w("AssetRepository: Failed to find entry", log_param("id", fmt_path(id)));
    return null;
  }

  AssetSource* src = alloc_alloc_t(g_alloc_heap, AssetSource);
  *src             = (AssetSource){.data = entry->data, .format = fmt};
  return src;
}

static void asset_repo_mem_destroy(AssetRepo* repo) {
  AssetRepoMem* repoMem = (AssetRepoMem*)repo;

  dynarray_for_t(&repoMem->entries, RepoEntry, entry) { string_free(g_alloc_heap, entry->data); };
  dynarray_destroy(&repoMem->entries);

  alloc_free_t(g_alloc_heap, repoMem);
}

AssetRepo* asset_repo_create_mem(const AssetMemRecord* records, const usize recordCount) {
  AssetRepoMem* repo = alloc_alloc_t(g_alloc_heap, AssetRepoMem);

  *repo = (AssetRepoMem){
      .api =
          {
              .open         = asset_source_mem_open,
              .destroy      = asset_repo_mem_destroy,
              .changesWatch = null,
              .changesPoll  = null,
          },
      .entries = dynarray_create_t(g_alloc_heap, RepoEntry, recordCount),
  };

  for (usize i = 0; i != recordCount; ++i) {
    RepoEntry entry = {
        .idHash = string_hash(records[i].id),
        .data   = string_dup(g_alloc_heap, records[i].data),
    };
    *dynarray_insert_sorted_t(&repo->entries, RepoEntry, asset_compare_entry, &entry) = entry;
  }
  return (AssetRepo*)repo;
}
