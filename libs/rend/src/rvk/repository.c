#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"

#include "repository_internal.h"

typedef enum {
  RvkRepositoryType_None,
  RvkRepositoryType_Texture,
} RvkRepositoryType;

typedef struct {
  RvkRepositoryType type;
  union {
    RvkTexture* texture;
  };
} RvkRepositoryEntry;

struct sRvkRepository {
  RvkRepositoryEntry entries[RvkRepositoryId_Count];
};

String rvk_repository_id_str(const RvkRepositoryId id) {
  static const String g_names[] = {
      string_static("MissingTexture"),
      string_static("MissingTextureCube"),
  };
  ASSERT(array_elems(g_names) == RvkRepositoryId_Count, "Incorrect number of names");
  return g_names[id];
}

RvkRepository* rvk_repository_create() {
  RvkRepository* repo = alloc_alloc_t(g_alloc_heap, RvkRepository);
  *repo               = (RvkRepository){0};
  return repo;
}

void rvk_repository_destroy(RvkRepository* repo) { alloc_free_t(g_alloc_heap, repo); }

void rvk_repository_texture_set(RvkRepository* repo, const RvkRepositoryId id, RvkTexture* tex) {
  repo->entries[id].type    = RvkRepositoryType_Texture;
  repo->entries[id].texture = tex;
}

RvkTexture* rvk_repository_texture_get(const RvkRepository* repo, const RvkRepositoryId id) {
  if (UNLIKELY(repo->entries[id].type != RvkRepositoryType_Texture)) {
    diag_crash_msg(
        "Repository asset '{}' cannot be found or is of the wrong type",
        fmt_text(rvk_repository_id_str(id)));
  }
  return repo->entries[id].texture;
}
