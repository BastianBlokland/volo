#pragma once
#include "core_types.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

// Internal forward declarations:
typedef struct sRvkGraphic     RvkGraphic;
typedef struct sRvkImage       RvkImage;
typedef struct sRvkMesh        RvkMesh;
typedef struct sRvkPass        RvkPass;
typedef struct sRvkSamplerSpec RvkSamplerSpec;

/**
 * Utility to submit draws.
 */
typedef struct sRendBuilder       RendBuilder;
typedef struct sRendBuilderBuffer RendBuilderBuffer;

RendBuilder* rend_builder_create(Allocator*);
void         rend_builder_destroy(RendBuilder*);

/**
 * Retrieve a thread-local buffer for the calling thread.
 * NOTE: Buffers should not be stored and/or shared between threads.
 */
RendBuilderBuffer* rend_builder_buffer(const RendBuilder*);

void rend_builder_clear(RendBuilderBuffer*);
void rend_builder_set_pass(RendBuilderBuffer*, RvkPass*);
void rend_builder_set_graphic(RendBuilderBuffer*, RvkGraphic*);
void rend_builder_set_vertex_count(RendBuilderBuffer*, u32 vertexCount);
void rend_builder_set_draw_mesh(RendBuilderBuffer*, RvkMesh*);
void rend_builder_set_draw_image(RendBuilderBuffer*, RvkImage*);
void rend_builder_set_draw_sampler(RendBuilderBuffer*, const RvkSamplerSpec*);
void rend_builder_push(RendBuilderBuffer*);
void rend_builder_discard(RendBuilderBuffer*);
void rend_builder_flush(RendBuilderBuffer*);
