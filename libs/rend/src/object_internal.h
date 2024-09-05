#pragma once
#include "ecs_module.h"
#include "rend_object.h"

// Internal forward declarations:
typedef struct sRendBuilderBuffer RendBuilderBuffer;
typedef struct sRendView          RendView;

ecs_comp_extern(RendSettingsComp);

void rend_object_draw(
    const RendObjectComp*, const RendView*, const RendSettingsComp*, RendBuilderBuffer*);