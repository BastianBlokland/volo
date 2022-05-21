#pragma once

// Internal forward declarations:
typedef struct sRvkTexture RvkTexture;
typedef struct sRvkGraphic RvkGraphic;

typedef enum {
  RvkRepositoryId_MissingTexture,
  RvkRepositoryId_MissingTextureCube,
  RvkRepositoryId_WireframeGraphic,

  RvkRepositoryId_Count,
} RvkRepositoryId;

typedef struct sRvkRepository RvkRepository;

RvkRepository* rvk_repository_create();
void           rvk_repository_destroy(RvkRepository*);

void rvk_repository_texture_set(RvkRepository*, RvkRepositoryId, RvkTexture*);
void rvk_repository_graphic_set(RvkRepository*, RvkRepositoryId, RvkGraphic*);

RvkTexture* rvk_repository_texture_get(const RvkRepository*, RvkRepositoryId);
RvkGraphic* rvk_repository_graphic_get(const RvkRepository*, RvkRepositoryId);
