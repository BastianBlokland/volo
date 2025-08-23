#pragma once
#include "ecs/module.h"

/**
 * Renderer errors.
 * NOTE: Errors are sorted by priority, lower errors will take precedence over higher onces.
 */
typedef enum {
  RendErrorType_VulkanNotFound,
  RendErrorType_DeviceNotFound,

  RendErrorType_Count,
} RendErrorType;

/**
 * Component that is added to the global entity when an error has been detected.
 */
ecs_comp_extern_public(RendErrorComp) { RendErrorType type; };

String rend_error_str(RendErrorType);
bool   rend_error_check(EcsWorld*);
void   rend_error_clear(EcsWorld*);
