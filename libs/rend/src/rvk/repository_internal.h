#pragma once

// Internal forward declarations:
typedef struct sRvkTexture RvkTexture;

typedef enum {
  RvkRepositoryId_MissingTexture,

  RvkRepositoryId_Count,
} RvkRepositoryId;

typedef struct sRvkRepository RvkRepository;

RvkRepository* rvk_repository_create();
void           rvk_repository_destroy(RvkRepository*);
void           rvk_repository_texture_set(RvkRepository*, RvkRepositoryId, RvkTexture*);
RvkTexture*    rvk_repository_texture_get(const RvkRepository*, RvkRepositoryId);
