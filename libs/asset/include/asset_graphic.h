#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  AssetGraphicTopology_Triangles, // Separate triangles with 3 vertices.
  AssetGraphicTopology_Lines,     // Separate lines with 2 vertices.
  AssetGraphicTopology_LineStrip, // Form lines between all vertices.

  AssetGraphicTopology_Count,
} AssetGraphicTopology;

typedef enum {
  AssetGraphicRasterizer_Fill,   // Fill the primitives with pixels.
  AssetGraphicRasterizer_Lines,  // Draw lines between the vertices.
  AssetGraphicRasterizer_Points, // Draw points on the vertices.

  AssetGraphicRasterizer_Count,
} AssetGraphicRasterizer;

typedef enum {
  AssetGraphicBlend_None,          // No blending, just replace the framebuffer's rgb values.
  AssetGraphicBlend_Alpha,         // Blend between rgb and the framebuffer based on the alpha.
  AssetGraphicBlend_Additive,      // Add rgb to the framebuffer (ignores alpha).
  AssetGraphicBlend_AlphaAdditive, // Multiply rgb by alpha and add them to the framebuffer.

  AssetGraphicBlend_Count,
} AssetGraphicBlend;

typedef enum {
  AssetGraphicWrap_Repeat, // Repeat the texture when sampling outside.
  AssetGraphicWrap_Clamp,  // Use the edge pixel when sampling outside.
} AssetGraphicWrap;

typedef enum {
  AssetGraphicFilter_Linear,  // Choose one of the pixels (sometimes known as 'point' filtering).
  AssetGraphicFilter_Nearest, // Linearly blend between neighboring pixels.
} AssetGraphicFilter;

typedef enum {
  AssetGraphicAniso_None, // No anisotropic filtering.
  AssetGraphicAniso_x2,   // Anisotropic filtering using 2 samples.
  AssetGraphicAniso_x4,   // Anisotropic filtering using 4 samples.
  AssetGraphicAniso_x8,   // Anisotropic filtering using 8 samples.
  AssetGraphicAniso_x16,  // Anisotropic filtering using 16 samples.
} AssetGraphicAniso;

typedef enum {
  AssetGraphicDepth_None,        // No depth-testing.
  AssetGraphicDepth_Less,        // Pass the depth-test if the fragment is closer.
  AssetGraphicDepth_LessOrEqual, // Pass the depth-test if the fragment is closer or equal.
  AssetGraphicDepth_Always,      // Always pass the depth-test.

  AssetGraphicDepth_Count,
} AssetGraphicDepth;

typedef enum {
  AssetGraphicCull_Back,  // Cull back-facing primitives.
  AssetGraphicCull_Front, // Cull front-facing primitives.
  AssetGraphicCull_None,  // No culling.

  AssetGraphicCull_Count,
} AssetGraphicCull;

typedef struct {
  String name;
  u32    binding;
  f64    value;
} AssetGraphicOverride;

typedef struct {
  String      shaderId;
  EcsEntityId shader;
  struct {
    AssetGraphicOverride* values;
    usize                 count;
  } overrides;
} AssetGraphicShader;

typedef struct {
  String             textureId;
  EcsEntityId        texture;
  AssetGraphicWrap   wrap;
  AssetGraphicFilter filter;
  AssetGraphicAniso  anisotropy;
} AssetGraphicSampler;

ecs_comp_extern_public(AssetGraphicComp) {
  struct {
    AssetGraphicShader* values;
    usize               count;
  } shaders;
  struct {
    AssetGraphicSampler* values;
    usize                count;
  } samplers;
  String                 meshId; // Mutally exclusive with 'vertexCount'.
  EcsEntityId            mesh;
  u32                    vertexCount; // Mutally exclusive with 'mesh'.
  AssetGraphicTopology   topology;
  AssetGraphicRasterizer rasterizer;
  u32                    lineWidth; // Line width (in pixels) when the rasterizer mode is 'lines'.
  AssetGraphicBlend      blend;
  AssetGraphicDepth      depth;
  AssetGraphicCull       cull;
};
