#pragma once
#include "core_memory.h"
#include "rend_settings.h"

#include "rvk/types_internal.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

// Forward declare from 'geo_color.h'.
typedef union uGeoColor GeoColor;

// Internal forward declarations:
typedef struct sRvkCanvas      RvkCanvas;
typedef struct sRvkGraphic     RvkGraphic;
typedef struct sRvkImage       RvkImage;
typedef struct sRvkMesh        RvkMesh;
typedef struct sRvkPass        RvkPass;
typedef struct sRvkRepository  RvkRepository;
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

bool rend_builder_canvas_push(
    RendBuilderBuffer*, RvkCanvas*, const RendSettingsComp*, RvkSize windowSize);
void rend_builder_canvas_flush(RendBuilderBuffer*);

const RvkRepository* rend_builder_repository(RendBuilderBuffer*);

RvkImage* rend_builder_img_swapchain(RendBuilderBuffer*);
void      rend_builder_img_clear_color(RendBuilderBuffer*, RvkImage*, GeoColor);
void      rend_builder_img_clear_depth(RendBuilderBuffer*, RvkImage*, f32 depth);
void      rend_builder_img_blit(RendBuilderBuffer*, RvkImage* src, RvkImage* dst);

void rend_builder_pass_push(RendBuilderBuffer*, RvkPass*);
void rend_builder_pass_flush(RendBuilderBuffer*);

RvkImage* rend_builder_attach_acquire_color(RendBuilderBuffer*, RvkPass*, u32 binding, RvkSize);
RvkImage* rend_builder_attach_acquire_depth(RendBuilderBuffer*, RvkPass*, RvkSize);
RvkImage* rend_builder_attach_acquire_copy(RendBuilderBuffer*, RvkImage*);
RvkImage* rend_builder_attach_acquire_copy_uninit(RendBuilderBuffer*, RvkImage*);
void      rend_builder_attach_release(RendBuilderBuffer*, RvkImage*);

void rend_builder_clear_color(RendBuilderBuffer*, GeoColor clearColor);

void rend_builder_attach_color(RendBuilderBuffer*, RvkImage*, u16 colorAttachIndex);
void rend_builder_attach_depth(RendBuilderBuffer*, RvkImage*);

Mem  rend_builder_global_data(RendBuilderBuffer*, u32 size, u16 dataIndex);
void rend_builder_global_image(RendBuilderBuffer*, RvkImage*, u16 imageIndex);
void rend_builder_global_image_frozen(RendBuilderBuffer*, const RvkImage*, u16 imageIndex);
void rend_builder_global_shadow(RendBuilderBuffer*, RvkImage*, u16 imageIndex);

void rend_builder_draw_push(RendBuilderBuffer*, const RvkGraphic*);
Mem  rend_builder_draw_data(RendBuilderBuffer*, u32 size);
u32  rend_builder_draw_instances_batch_size(RendBuilderBuffer*, u32 dataStride);
Mem  rend_builder_draw_instances(RendBuilderBuffer*, u32 dataStride, u32 count);
void rend_builder_draw_vertex_count(RendBuilderBuffer*, u32 vertexCount);
void rend_builder_draw_mesh(RendBuilderBuffer*, const RvkMesh*);
void rend_builder_draw_image(RendBuilderBuffer*, RvkImage*);
void rend_builder_draw_image_frozen(RendBuilderBuffer*, const RvkImage*);
void rend_builder_draw_sampler(RendBuilderBuffer*, RvkSamplerSpec);
void rend_builder_draw_flush(RendBuilderBuffer*);
