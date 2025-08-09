#pragma once
#include "ecs_module.h"

/**
 * Renderer errors.
 * NOTE: Errors are sorted by priority, higher errors will take precedence over lower onces.
 */
typedef enum {
  RendErrorType_DeviceNotFound,

  RendErrorType_Count,
} RendErrorType;

/**
 * Component that is added to the global entity when an error has been detected.
 */
ecs_comp_extern_public(RendErrorComp) { RendErrorType type; };

String rend_error_str(RendErrorType);
void   rend_error_clear(EcsWorld*);
