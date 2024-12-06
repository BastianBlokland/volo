#pragma once
#include "core.h"
#include "ecs_module.h"

/**
 * Forward header for the gap library.
 */

ecs_comp_extern(GapWindowComp);

typedef enum eGapCursor       GapCursor;
typedef enum eGapIcon         GapIcon;
typedef enum eGapKey          GapKey;
typedef enum eGapParam        GapParam;
typedef enum eGapWindowEvents GapWindowEvents;
typedef enum eGapWindowFlags  GapWindowFlags;
typedef enum eGapWindowMode   GapWindowMode;
typedef union uGapVector      GapVector;
