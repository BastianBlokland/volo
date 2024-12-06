#pragma once
#include "asset_texture.h"
#include "core_string.h"

#include "forward_internal.h"
#include "image_internal.h"
#include "transfer_internal.h"

typedef struct sRvkTexture {
  RvkImage      image;
  RvkTransferId pixelTransfer;
} RvkTexture;

RvkTexture* rvk_texture_create(RvkDevice*, const AssetTextureComp*, String dbgName);
void        rvk_texture_destroy(RvkTexture*, RvkDevice*);
RvkDescKind rvk_texture_sampler_kind(const RvkTexture*);
bool        rvk_texture_is_ready(const RvkTexture*, const RvkDevice*);
