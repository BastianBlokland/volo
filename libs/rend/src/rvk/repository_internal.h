#pragma once

// Internal forward declarations:
typedef struct sRvkTexture RvkTexture;
typedef struct sRvkGraphic RvkGraphic;

typedef enum {
  RvkRepositoryId_AmbientDebugGraphic,
  RvkRepositoryId_AmbientGraphic,
  RvkRepositoryId_AmbientOcclusionGraphic,
  RvkRepositoryId_BloomDownGraphic,
  RvkRepositoryId_BloomUpGraphic,
  RvkRepositoryId_DebugImageViewerCubeGraphic,
  RvkRepositoryId_DebugImageViewerGraphic,
  RvkRepositoryId_DebugMeshViewerGraphic,
  RvkRepositoryId_DebugSkinningGraphic,
  RvkRepositoryId_DebugWireframeGraphic,
  RvkRepositoryId_DebugWireframeSkinnedGraphic,
  RvkRepositoryId_DebugWireframeTerrainGraphic,
  RvkRepositoryId_FogBlurHorGraphic,
  RvkRepositoryId_FogBlurVerGraphic,
  RvkRepositoryId_FogGraphic,
  RvkRepositoryId_MissingTexture,
  RvkRepositoryId_MissingTextureCube,
  RvkRepositoryId_OutlineGraphic,
  RvkRepositoryId_ShadowClipGraphic,
  RvkRepositoryId_ShadowGraphic,
  RvkRepositoryId_ShadowSkinnedGraphic,
  RvkRepositoryId_ShadowVfxSpriteGraphic,
  RvkRepositoryId_SkyCubeMapGraphic,
  RvkRepositoryId_SkyGradientGraphic,
  RvkRepositoryId_TonemapperGraphic,

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
