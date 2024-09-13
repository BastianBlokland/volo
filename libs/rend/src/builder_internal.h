#pragma once
#include "core_memory.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

// Forward declare from 'geo_color.h'.
typedef union uGeoColor GeoColor;

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

void rend_builder_pass_push(RendBuilderBuffer*, RvkPass*);
void rend_builder_pass_flush(RendBuilderBuffer*);

void rend_builder_clear_color(RendBuilderBuffer*, GeoColor clearColor);

void rend_builder_attach_color(RendBuilderBuffer*, RvkImage*, u16 colorAttachIndex);
void rend_builder_attach_depth(RendBuilderBuffer*, RvkImage*);

void rend_builder_global_data(RendBuilderBuffer*, Mem, u16 dataIndex);
void rend_builder_global_image(RendBuilderBuffer*, RvkImage*, u16 imageIndex);
void rend_builder_global_shadow(RendBuilderBuffer*, RvkImage*, u16 imageIndex);

void rend_builder_draw_push(RendBuilderBuffer*, const RvkGraphic*);
void rend_builder_draw_data(RendBuilderBuffer*, Mem drawData);
u32  rend_builder_draw_instances_batch_size(RendBuilderBuffer*, u32 instDataStride);
void rend_builder_draw_instances_add(RendBuilderBuffer*, Mem data, u32 count);
void rend_builder_draw_vertex_count(RendBuilderBuffer*, u32 vertexCount);
void rend_builder_draw_mesh(RendBuilderBuffer*, const RvkMesh*);
void rend_builder_draw_image(RendBuilderBuffer*, RvkImage*);
void rend_builder_draw_sampler(RendBuilderBuffer*, RvkSamplerSpec);
void rend_builder_draw_flush(RendBuilderBuffer*);
