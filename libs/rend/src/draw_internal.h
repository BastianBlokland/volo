#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

#include "rvk/pass_internal.h"

ecs_comp_extern(RendDrawComp);

RendDrawComp* rend_draw_create(EcsWorld*, EcsEntityId entity);
EcsEntityId   rend_draw_graphic(const RendDrawComp*);
bool          rend_draw_gather(RendDrawComp*);
RvkPassDraw   rend_draw_output(const RendDrawComp*, RvkGraphic* graphic);

void rend_draw_set_graphic(RendDrawComp*, EcsEntityId graphic);
void rend_draw_set_vertex_count(RendDrawComp*, u32 vertexCount);

/**
 * Update the data size per instance for the given draw.
 * NOTE: Clears all added instances.
 */
void rend_draw_set_data_size(RendDrawComp*, u32 size);

Mem rend_draw_add_instance(RendDrawComp*);
