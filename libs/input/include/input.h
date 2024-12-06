#pragma once
#include "core.h"
#include "ecs_module.h"

/**
 * Forward header for the input library.
 */

ecs_comp_extern(InputManagerComp);
ecs_comp_extern(InputResourceComp);

typedef enum eInputBlocker    InputBlocker;
typedef enum eInputCursorMode InputCursorMode;
typedef enum eInputModifier   InputModifier;
