#pragma once
#include "ecs/module.h"
#include "rend/object.h"

#include "forward_internal.h"

ecs_comp_extern(RendSettingsComp);

void rend_object_draw(
    const RendObjectComp*, const RendView*, const RendSettingsComp*, RendBuilder*);
