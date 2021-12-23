#pragma once
#include "asset_texture.h"

#include "image_internal.h"
#include "transfer_internal.h"

// Internal forward declarations:
typedef struct sRvkDevice RvkDevice;
typedef struct sRvkPass   RvkPass;

typedef enum {
  RvkTextureFlags_Ready = 1 << 0,
} RvkTextureFlags;

typedef struct sRvkTexture {
  RvkDevice*      device;
  RvkImage        image;
  RvkTextureFlags flags;
  RvkTransferId   pixelTransfer;
} RvkTexture;

RvkTexture* rvk_texture_create(RvkDevice*, const AssetTextureComp*, String dbgName);
void        rvk_texture_destroy(RvkTexture*);
bool        rvk_texture_prepare(RvkTexture*, VkCommandBuffer);
