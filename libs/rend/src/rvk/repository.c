#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"

#include "repository_internal.h"

typedef enum {
  RvkRepositoryType_None,
  RvkRepositoryType_Texture,
  RvkRepositoryType_Mesh,
  RvkRepositoryType_Graphic,
} RvkRepositoryType;

typedef struct {
  RvkRepositoryType type;
  union {
    const RvkTexture* texture;
    const RvkMesh*    mesh;
    const RvkGraphic* graphic;
  };
} RvkRepositoryEntry;

struct sRvkRepository {
  RvkRepositoryEntry entries[RvkRepositoryId_Count];
};

String rvk_repository_id_str(const RvkRepositoryId id) {
  static const String g_names[] = {
      string_static("AmbientDebugGraphic"),
      string_static("AmbientGraphic"),
      string_static("AmbientOcclusionGraphic"),
      string_static("BloomDownGraphic"),
      string_static("BloomUpGraphic"),
      string_static("DebugImageViewerCubeGraphic"),
      string_static("DebugImageViewerGraphic"),
      string_static("DebugMeshViewerGraphic"),
      string_static("FogBlurHorGraphic"),
      string_static("FogBlurVerGraphic"),
      string_static("FogGraphic"),
      string_static("MissingMesh"),
      string_static("MissingTexture"),
      string_static("MissingTextureCube"),
      string_static("OutlineGraphic"),
      string_static("SkyCubeMapGraphic"),
      string_static("SkyGradientGraphic"),
      string_static("TonemapperGraphic"),
      string_static("WhiteTexture"),
  };
  ASSERT(array_elems(g_names) == RvkRepositoryId_Count, "Incorrect number of names");
  return g_names[id];
}

RvkRepository* rvk_repository_create(void) {
  RvkRepository* repo = alloc_alloc_t(g_allocHeap, RvkRepository);
  *repo               = (RvkRepository){0};
  return repo;
}

void rvk_repository_destroy(RvkRepository* repo) { alloc_free_t(g_allocHeap, repo); }

void rvk_repository_texture_set(RvkRepository* r, const RvkRepositoryId id, const RvkTexture* tex) {
  diag_assert(!r->entries[id].type || r->entries[id].type == RvkRepositoryType_Texture);
  r->entries[id].type    = RvkRepositoryType_Texture;
  r->entries[id].texture = tex;
}

void rvk_repository_mesh_set(RvkRepository* r, const RvkRepositoryId id, const RvkMesh* mesh) {
  diag_assert(!r->entries[id].type || r->entries[id].type == RvkRepositoryType_Mesh);
  r->entries[id].type = RvkRepositoryType_Mesh;
  r->entries[id].mesh = mesh;
}

void rvk_repository_graphic_set(RvkRepository* r, const RvkRepositoryId id, const RvkGraphic* gra) {
  diag_assert(!r->entries[id].type || r->entries[id].type == RvkRepositoryType_Graphic);
  r->entries[id].type    = RvkRepositoryType_Graphic;
  r->entries[id].graphic = gra;
}

void rvk_repository_unset(RvkRepository* r, const RvkRepositoryId id) {
  switch (r->entries[id].type) {
  case RvkRepositoryType_Texture:
    r->entries[id].texture = null;
    break;
  case RvkRepositoryType_Mesh:
    r->entries[id].mesh = null;
    break;
  case RvkRepositoryType_Graphic:
    r->entries[id].graphic = null;
    break;
  case RvkRepositoryType_None:
    break;
  }
}

bool rvk_repository_is_set(const RvkRepository* r, const RvkRepositoryId id) {
  return r->entries[id].type != RvkRepositoryType_None;
}

const RvkTexture* rvk_repository_texture_get(const RvkRepository* r, const RvkRepositoryId id) {
  if (UNLIKELY(r->entries[id].type != RvkRepositoryType_Texture)) {
    return null;
  }
  return r->entries[id].texture;
}

const RvkMesh* rvk_repository_mesh_get(const RvkRepository* r, const RvkRepositoryId id) {
  if (UNLIKELY(r->entries[id].type != RvkRepositoryType_Mesh)) {
    return null;
  }
  return r->entries[id].mesh;
}

const RvkGraphic* rvk_repository_graphic_get(const RvkRepository* r, const RvkRepositoryId id) {
  if (UNLIKELY(r->entries[id].type != RvkRepositoryType_Graphic)) {
    return null;
  }
  return r->entries[id].graphic;
}
