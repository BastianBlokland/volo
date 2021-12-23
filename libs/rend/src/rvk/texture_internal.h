#pragma once
#include "asset_texture.h"

#include "image_internal.h"
#include "transfer_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;
typedef struct sRvkPass   RvkPass;

typedef struct sRvkTexture {
  RvkDevice*    device;
  RvkImage      image;
  RvkTransferId pixelTransfer;
} RvkTexture;

RvkTexture* rvk_texture_create(RvkDevice*, const AssetTextureComp*);
void        rvk_texture_destroy(RvkTexture*);
bool        rvk_texture_prepare(RvkTexture*, VkCommandBuffer);
