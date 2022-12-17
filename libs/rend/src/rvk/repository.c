#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"

#include "repository_internal.h"

typedef enum {
  RvkRepositoryType_None,
  RvkRepositoryType_Texture,
  RvkRepositoryType_Graphic,
} RvkRepositoryType;

typedef struct {
  RvkRepositoryType type;
  union {
    RvkTexture* texture;
    RvkGraphic* graphic;
  };
} RvkRepositoryEntry;

struct sRvkRepository {
  RvkRepositoryEntry entries[RvkRepositoryId_Count];
};

String rvk_repository_id_str(const RvkRepositoryId id) {
  static const String g_names[] = {
      string_static("MissingTexture"),
      string_static("MissingTextureCube"),
      string_static("WireframeGraphic"),
      string_static("WireframeSkinnedGraphic"),
      string_static("WireframeTerrainGraphic"),
      string_static("DebugSkinningGraphic"),
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
  diag_assert(!repo->entries[id].type || repo->entries[id].type == RvkRepositoryType_Texture);
  repo->entries[id].type    = RvkRepositoryType_Texture;
  repo->entries[id].texture = tex;
}

void rvk_repository_graphic_set(RvkRepository* repo, const RvkRepositoryId id, RvkGraphic* gra) {
  diag_assert(!repo->entries[id].type || repo->entries[id].type == RvkRepositoryType_Graphic);
  repo->entries[id].type    = RvkRepositoryType_Graphic;
  repo->entries[id].graphic = gra;
}

void rvk_repository_unset(RvkRepository* repo, const RvkRepositoryId id) {
  switch (repo->entries[id].type) {
  case RvkRepositoryType_Texture:
    repo->entries[id].texture = null;
    break;
  case RvkRepositoryType_Graphic:
    repo->entries[id].graphic = null;
    break;
  case RvkRepositoryType_None:
    break;
  }
}

RvkTexture* rvk_repository_texture_get(const RvkRepository* repo, const RvkRepositoryId id) {
  if (UNLIKELY(repo->entries[id].type != RvkRepositoryType_Texture)) {
    diag_crash_msg(
        "Repository asset '{}' cannot be found or is of the wrong type",
        fmt_text(rvk_repository_id_str(id)));
  }
  return repo->entries[id].texture;
}

RvkGraphic* rvk_repository_graphic_get(const RvkRepository* repo, const RvkRepositoryId id) {
  if (UNLIKELY(repo->entries[id].type != RvkRepositoryType_Graphic)) {
    diag_crash_msg(
        "Repository asset '{}' cannot be found or is of the wrong type",
        fmt_text(rvk_repository_id_str(id)));
  }
  return repo->entries[id].graphic;
}
