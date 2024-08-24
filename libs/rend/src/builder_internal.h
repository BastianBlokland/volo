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
typedef struct sRendBuilder RendBuilder;

RendBuilder* rend_builder_create(Allocator*);
void         rend_builder_destroy(RendBuilder*);
