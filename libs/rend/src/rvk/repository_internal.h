#pragma once
#include "core.h"

#include "forward_internal.h"

typedef enum {
  RvkRepositoryId_AmbientDebugGraphic,
  RvkRepositoryId_AmbientGraphic,
  RvkRepositoryId_AmbientOcclusionGraphic,
  RvkRepositoryId_BloomDownGraphic,
  RvkRepositoryId_BloomUpGraphic,
  RvkRepositoryId_DebugImageViewerArrayGraphic,
  RvkRepositoryId_DebugImageViewerCubeGraphic,
  RvkRepositoryId_DebugImageViewerGraphic,
  RvkRepositoryId_DebugMeshViewerGraphic,
  RvkRepositoryId_FogBlurHorGraphic,
  RvkRepositoryId_FogBlurVerGraphic,
  RvkRepositoryId_FogGraphic,
  RvkRepositoryId_MissingMesh,
  RvkRepositoryId_MissingTexture,
  RvkRepositoryId_MissingTextureArray,
  RvkRepositoryId_MissingTextureCube,
  RvkRepositoryId_OutlineGraphic,
  RvkRepositoryId_SkyCubeMapGraphic,
  RvkRepositoryId_SkyGradientGraphic,
  RvkRepositoryId_TonemapperGraphic,
  RvkRepositoryId_WhiteTexture,

  RvkRepositoryId_Count,
} RvkRepositoryId;

typedef struct sRvkRepository RvkRepository;

RvkRepository* rvk_repository_create(void);
void           rvk_repository_destroy(RvkRepository*);

void rvk_repository_texture_set(RvkRepository*, RvkRepositoryId, const RvkTexture*);
void rvk_repository_mesh_set(RvkRepository*, RvkRepositoryId, const RvkMesh*);
void rvk_repository_graphic_set(RvkRepository*, RvkRepositoryId, const RvkGraphic*);

void rvk_repository_unset(RvkRepository*, RvkRepositoryId);

bool rvk_repository_is_set(const RvkRepository*, RvkRepositoryId);
bool rvk_repository_all_set(const RvkRepository*);

const RvkTexture* rvk_repository_texture_get(const RvkRepository*, RvkRepositoryId);
const RvkMesh*    rvk_repository_mesh_get(const RvkRepository*, RvkRepositoryId);
const RvkGraphic* rvk_repository_graphic_get(const RvkRepository*, RvkRepositoryId);
