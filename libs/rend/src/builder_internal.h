#pragma once

// Forward declare from 'core_alloc.h'.
typedef struct sAllocator Allocator;

// Internal forward declarations:
typedef struct sRvkGraphic RvkGraphic;
typedef struct sRvkImage   RvkImage;
typedef struct sRvkMesh    RvkMesh;
typedef struct sRvkPass    RvkPass;

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
