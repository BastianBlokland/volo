#pragma once
#include "core_memory.h"
#include "geo.h"
#include "rend_settings.h"

#include "rvk/types_internal.h"

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

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
typedef struct sRendBuilderContainer RendBuilderContainer;
typedef struct sRendBuilder          RendBuilder;

RendBuilderContainer* rend_builder_container_create(Allocator*);
void                  rend_builder_container_destroy(RendBuilderContainer*);

/**
 * Retrieve a thread-local builder for the calling thread.
 * NOTE: Builders should not be stored and/or shared between threads.
 */
RendBuilder* rend_builder(const RendBuilderContainer*);

bool rend_builder_canvas_push(
    RendBuilder*, RvkCanvas*, const RendSettingsComp*, RvkSize windowSize);
void rend_builder_canvas_flush(RendBuilder*);

const RvkRepository* rend_builder_repository(RendBuilder*);

RvkImage* rend_builder_img_swapchain(RendBuilder*);
void      rend_builder_img_clear_color(RendBuilder*, RvkImage*, GeoColor);
void      rend_builder_img_clear_depth(RendBuilder*, RvkImage*, f32 depth);
void      rend_builder_img_blit(RendBuilder*, RvkImage* src, RvkImage* dst);

void rend_builder_phase_output(RendBuilder*); // Advance to the output phase.

void rend_builder_pass_push(RendBuilder*, RvkPass*);
void rend_builder_pass_flush(RendBuilder*);

RvkImage* rend_builder_attach_acquire_color(RendBuilder*, RvkPass*, u32 binding, RvkSize);
RvkImage* rend_builder_attach_acquire_depth(RendBuilder*, RvkPass*, RvkSize);
RvkImage* rend_builder_attach_acquire_copy(RendBuilder*, RvkImage* src);
RvkImage* rend_builder_attach_acquire_copy_uninit(RendBuilder*, RvkImage* src);
void      rend_builder_attach_release(RendBuilder*, RvkImage*);

void rend_builder_clear_color(RendBuilder*, GeoColor clearColor);

void rend_builder_attach_color(RendBuilder*, RvkImage*, u16 colorAttachIndex);
void rend_builder_attach_depth(RendBuilder*, RvkImage*);

Mem  rend_builder_global_data(RendBuilder*, u32 size, u16 dataIndex);
void rend_builder_global_image(RendBuilder*, RvkImage*, u16 imageIndex);
void rend_builder_global_image_frozen(RendBuilder*, const RvkImage*, u16 imageIndex);
void rend_builder_global_shadow(RendBuilder*, RvkImage*, u16 imageIndex);

void rend_builder_draw_push(RendBuilder*, const RvkGraphic*);
Mem  rend_builder_draw_data(RendBuilder*, u32 size);
u32  rend_builder_draw_instances_batch_size(RendBuilder*, u32 dataStride);
Mem  rend_builder_draw_instances(RendBuilder*, u32 dataStride, u32 count);
void rend_builder_draw_vertex_count(RendBuilder*, u32 vertexCount);
void rend_builder_draw_mesh(RendBuilder*, const RvkMesh*);
void rend_builder_draw_image(RendBuilder*, RvkImage*);
void rend_builder_draw_image_frozen(RendBuilder*, const RvkImage*);
void rend_builder_draw_sampler(RendBuilder*, RvkSamplerSpec);
void rend_builder_draw_flush(RendBuilder*);
