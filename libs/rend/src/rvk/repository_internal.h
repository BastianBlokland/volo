#pragma once

// Internal forward declarations:
typedef struct sRvkTexture RvkTexture;
typedef struct sRvkGraphic RvkGraphic;

typedef enum {
  RvkRepositoryId_MissingTexture,
  RvkRepositoryId_MissingTextureCube,
  RvkRepositoryId_ShadowGraphic,
  RvkRepositoryId_ShadowSkinnedGraphic,
  RvkRepositoryId_WireframeGraphic,
  RvkRepositoryId_WireframeSkinnedGraphic,
  RvkRepositoryId_WireframeTerrainGraphic,
  RvkRepositoryId_TonemapperGraphic,
  RvkRepositoryId_OutlineGraphic,
  RvkRepositoryId_DebugSkinningGraphic,
  RvkRepositoryId_DebugShadowGraphic,
  RvkRepositoryId_AmbientGraphic,
  RvkRepositoryId_AmbientDebugGraphic,
  RvkRepositoryId_AmbientOcclusionGraphic,
  RvkRepositoryId_SkyGraphic,
  RvkRepositoryId_BloomDownGraphic,

  RvkRepositoryId_Count,
} RvkRepositoryId;

typedef struct sRvkRepository RvkRepository;

RvkRepository* rvk_repository_create();
void           rvk_repository_destroy(RvkRepository*);

void rvk_repository_texture_set(RvkRepository*, RvkRepositoryId, RvkTexture*);
void rvk_repository_graphic_set(RvkRepository*, RvkRepositoryId, RvkGraphic*);

void rvk_repository_unset(RvkRepository*, RvkRepositoryId);

RvkTexture* rvk_repository_texture_get(const RvkRepository*, RvkRepositoryId);
RvkGraphic* rvk_repository_graphic_get(const RvkRepository*, RvkRepositoryId);
RvkGraphic* rvk_repository_graphic_get_maybe(const RvkRepository*, RvkRepositoryId);
