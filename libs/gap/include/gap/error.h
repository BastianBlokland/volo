#pragma once
#include "ecs/module.h"

/**
 * Gui Application Protocol errors.
 * NOTE: Errors are sorted by priority, lower errors will take precedence over higher onces.
 */
typedef enum {
  GapErrorType_PlatformInitFailed,

  GapErrorType_Count,
} GapErrorType;

/**
 * Component that is added to the global entity when an error has been detected.
 */
ecs_comp_extern_public(GapErrorComp) { GapErrorType type; };

String gap_error_str(GapErrorType);
bool   gap_error_check(EcsWorld*);
void   gap_error_clear(EcsWorld*);
