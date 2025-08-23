#pragma once
#include "asset/texture.h"
#include "core/string.h"

#include "forward.h"
#include "image.h"
#include "transfer.h"

typedef struct sRvkTexture {
  RvkImage      image;
  RvkTransferId pixelTransfer;
} RvkTexture;

RvkTexture* rvk_texture_create(RvkDevice*, const AssetTextureComp*, String dbgName);
void        rvk_texture_destroy(RvkTexture*, RvkDevice*);
RvkDescKind rvk_texture_sampler_kind(const RvkTexture*);
bool        rvk_texture_is_ready(const RvkTexture*, const RvkDevice*);
