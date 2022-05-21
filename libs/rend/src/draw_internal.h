#pragma once
#include "rend_draw.h"

#include "rvk/pass_internal.h"
#include "view_internal.h"

bool        rend_draw_gather(RendDrawComp*, const RendView*, const RendSettingsComp*);
RvkPassDraw rend_draw_output(const RendDrawComp*, RvkGraphic* graphic, RvkMesh* dynMesh);
