#pragma once
#include "asset_texture.h"
#include "core_string.h"

#include "image_internal.h"
#include "transfer_internal.h"

// Internal forward declarations:
typedef enum eRvkDescKind RvkDescKind;
typedef struct sRvkDevice RvkDevice;
typedef struct sRvkPass   RvkPass;

typedef struct sRvkTexture {
  RvkDevice*    device;
  String        dbgName;
  RvkImage      image;
  RvkTransferId pixelTransfer;
} RvkTexture;

RvkTexture* rvk_texture_create(RvkDevice*, const AssetTextureComp*, String dbgName);
void        rvk_texture_destroy(RvkTexture*);
RvkDescKind rvk_texture_sampler_kind(RvkTexture*);
bool        rvk_texture_is_ready(const RvkTexture*);
