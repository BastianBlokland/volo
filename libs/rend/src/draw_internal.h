#pragma once
#include "core_dynarray.h"
#include "ecs_entity.h"
#include "ecs_module.h"

ecs_comp_extern_public(RendDrawComp) {
  EcsEntityId graphic;
  u32         vertexCountOverride;
  DynArray    instances;
};

RendDrawComp* rend_draw_create(EcsWorld*, EcsEntityId entity);

EcsEntityId rend_draw_graphic(const RendDrawComp*);
u32         rend_draw_instance_count(const RendDrawComp*);

void rend_draw_set_graphic(RendDrawComp*, EcsEntityId graphic);
void rend_draw_set_vertex_count(RendDrawComp*, u32 vertexCount);

/**
 * Update the data size per instance for the given draw.
 * NOTE: Clears all added instances.
 */
void rend_draw_set_data_size(RendDrawComp*, u32 size);

Mem rend_draw_add_instance(RendDrawComp*);
