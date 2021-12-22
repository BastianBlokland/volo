#pragma once
#include "asset_texture.h"

#include "image_internal.h"
#include "transfer_internal.h"

// Internal forward declarations:
typedef struct sRvkPlatform RvkPlatform;
typedef struct sRvkPass     RvkPass;

typedef struct sRvkTexture {
  RvkPlatform*  platform;
  RvkImage      image;
  RvkTransferId pixelTransfer;
} RvkTexture;

RvkTexture* rvk_texture_create(RvkPlatform*, const AssetTextureComp*);
void        rvk_texture_destroy(RvkTexture*);
bool        rvk_texture_prepare(RvkTexture*, const RvkPass*);
