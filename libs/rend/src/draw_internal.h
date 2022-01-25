#pragma once
#include "core_dynarray.h"
#include "ecs_entity.h"
#include "ecs_module.h"

ecs_comp_extern_public(RendDrawComp) {
  EcsEntityId graphic;
  u32         vertexCountOverride;
  DynArray    instances;
};

EcsEntityId rend_draw_graphic(const RendDrawComp*);
u32         rend_draw_instance_count(const RendDrawComp*);

/**
 * Provide an explicit vertex count for this draw.
 * NOTE: Will override the vertex count configured in the graphic.
 */
void rend_draw_set_vertex_count(RendDrawComp*, u32 vertexCount);

/**
 * Update the data size per instance for the given draw.
 * NOTE: Clears all added instances.
 */
void rend_draw_set_data_size(RendDrawComp*, u32 size);

/**
 * Add another instance to the given draw.
 */
Mem rend_draw_add_instance(RendDrawComp*);
