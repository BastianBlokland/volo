#pragma once
#include "rend_settings.h"
#include "vulkan_api.h"

typedef struct sRvkLib {
  VkInstance            vkInst;
  VkAllocationCallbacks vkAlloc;
} RvkLib;

RvkLib* rvk_lib_create(const RendSettingsGlobalComp*);
void    rvk_lib_destroy(RvkLib*);
