#pragma once
#include "rend_settings.h"
#include "vulkan_api.h"

typedef enum {
  RvkLibFlags_Validation   = 1 << 0,
  RvkLibFlags_Debug        = 1 << 1,
  RvkLibFlags_DebugVerbose = 1 << 2,
} RvkLibFlags;

typedef struct sRvkLib {
  RvkLibFlags              flags;
  VkInterfaceInstance      api;
  DynLib*                  vulkanLib;
  VkInstance               vkInst;
  VkAllocationCallbacks    vkAlloc;
  VkDebugUtilsMessengerEXT vkMessenger;
} RvkLib;

RvkLib* rvk_lib_create(const RendSettingsGlobalComp*);
void    rvk_lib_destroy(RvkLib*);

#define rvk_call(_OBJ_, _FUNC_, ...) (_OBJ_)->api._FUNC_(__VA_ARGS__)

#define rvk_call_checked(_OBJ_, _FUNC_, ...)                                                       \
  rvk_api_check(string_lit(#_FUNC_), (_OBJ_)->api._FUNC_(__VA_ARGS__))

void rvk_api_check(String func, VkResult);
