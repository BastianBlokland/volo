#pragma once
#include "core.h"
#include "ecs_module.h"

ecs_comp_extern(SndMixerComp);

typedef enum eSndChannel       SndChannel;
typedef enum eSndResult        SndResult;
typedef struct sSndBuffer      SndBuffer;
typedef struct sSndBufferFrame SndBufferFrame;
typedef struct sSndBufferView  SndBufferView;
typedef u32                    SndObjectId;
