#pragma once
#include "rend_settings.h"
#include "vulkan_api.h"

typedef enum {
  RvkLibFlags_Validation = 1 << 0,
  RvkLibFlags_Debug      = 1 << 1,
} RvkLibFlags;

typedef struct sRvkLib {
  RvkLibFlags           flags;
  VkInstance            vkInst;
  VkAllocationCallbacks vkAlloc;
} RvkLib;

RvkLib* rvk_lib_create(const RendSettingsGlobalComp*);
void    rvk_lib_destroy(RvkLib*);
