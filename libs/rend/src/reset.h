#pragma once
#include "rend/reset.h"

ecs_comp_extern(RendResetComp);

bool rend_will_reset(EcsWorld*);
