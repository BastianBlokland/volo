#pragma once

// Internal forward declarations:
typedef struct sRvkTexture RvkTexture;
typedef struct sRvkGraphic RvkGraphic;

typedef enum {
  RvkRepositoryId_MissingTexture,
  RvkRepositoryId_MissingTextureCube,
  RvkRepositoryId_ShadowGraphic,
  RvkRepositoryId_ShadowSkinnedGraphic,
  RvkRepositoryId_ShadowClipGraphic,
  RvkRepositoryId_ShadowVfxSpriteGraphic,
  RvkRepositoryId_TonemapperGraphic,
  RvkRepositoryId_FogGraphic,
  RvkRepositoryId_OutlineGraphic,
  RvkRepositoryId_AmbientGraphic,
  RvkRepositoryId_AmbientDebugGraphic,
  RvkRepositoryId_AmbientOcclusionGraphic,
  RvkRepositoryId_SkyGradientGraphic,
  RvkRepositoryId_SkyCubeMapGraphic,
  RvkRepositoryId_BloomDownGraphic,
  RvkRepositoryId_BloomUpGraphic,
  RvkRepositoryId_BlurHorGraphic,
  RvkRepositoryId_BlurVerGraphic,
  RvkRepositoryId_DebugImageViewerGraphic,
  RvkRepositoryId_DebugImageViewerCubeGraphic,
  RvkRepositoryId_DebugMeshViewerGraphic,
  RvkRepositoryId_DebugWireframeGraphic,
  RvkRepositoryId_DebugWireframeSkinnedGraphic,
  RvkRepositoryId_DebugWireframeTerrainGraphic,
  RvkRepositoryId_DebugSkinningGraphic,

  RvkRepositoryId_Count,
} RvkRepositoryId;

typedef struct sRvkRepository RvkRepository;

RvkRepository* rvk_repository_create(void);
void           rvk_repository_destroy(RvkRepository*);

void rvk_repository_texture_set(RvkRepository*, RvkRepositoryId, const RvkTexture*);
void rvk_repository_graphic_set(RvkRepository*, RvkRepositoryId, const RvkGraphic*);

void rvk_repository_unset(RvkRepository*, RvkRepositoryId);

const RvkTexture* rvk_repository_texture_get(const RvkRepository*, RvkRepositoryId);
const RvkGraphic* rvk_repository_graphic_get(const RvkRepository*, RvkRepositoryId);
const RvkGraphic* rvk_repository_graphic_get_maybe(const RvkRepository*, RvkRepositoryId);
