#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  AssetMaterialTopology_Triangles, // Separate triangles with 3 vertices.
  AssetMaterialTopology_Lines,     // Separate lines with 2 vertices.
  AssetMaterialTopology_LineStrip, // From lines between all vertices.
} AssetMaterialTopology;

typedef enum {
  AssetMaterialRasterizer_Fill,   // Fill the primitives with pixels.
  AssetMaterialRasterizer_Lines,  // Draw lines between the vertices.
  AssetMaterialRasterizer_Points, // Draw points on the vertices.
} AssetMaterialRasterizer;

typedef enum {
  AssetMaterialBlend_None,          // No blending, just replace the framebuffer's rgb values.
  AssetMaterialBlend_Alpha,         // Blend between rgb and the framebuffer based on the alpha.
  AssetMaterialBlend_Additive,      // Add rgb to the framebuffer (ignores alpha).
  AssetMaterialBlend_AlphaAdditive, // Multiply rgb by alpha and add them to the framebuffer.
} AssetMaterialBlend;

typedef enum {
  AssetMaterialWrap_Repeat, // Repeat the texture when sampling outside.
  AssetMaterialWrap_Clamp,  // Use the edge pixel when sampling outside.
} AssetMaterialWrap;

typedef enum {
  AssetMaterialFilter_Nearest, // Linearly blend between neighboring pixels.
  AssetMaterialFilter_Linear,  // Choose one of the pixels (sometimes known as 'point' filtering).
} AssetMaterialFilter;

typedef enum {
  AssetMaterialAniso_None, // No anisotropic filtering.
  AssetMaterialAniso_x2,   // Anisotropic filtering using 2 samples.
  AssetMaterialAniso_x4,   // Anisotropic filtering using 4 samples.
  AssetMaterialAniso_x8,   // Anisotropic filtering using 8 samples.
  AssetMaterialAniso_x16,  // Anisotropic filtering using 16 samples.
} AssetMaterialAniso;

typedef enum {
  AssetMaterialDepth_None,   // No depth-testing.
  AssetMaterialDepth_Less,   // Pass the depth-test if the fragment is closer.
  AssetMaterialDepth_Always, // Always pass the depth-test.
} AssetMaterialDepth;

typedef enum {
  AssetMaterialCull_None,  // No culling.
  AssetMaterialCull_Back,  // Cull back-facing primitives.
  AssetMaterialCull_Front, // Cull front-facing primitives.
} AssetMaterialCull;

typedef struct {
  EcsEntityId         textureAsset;
  AssetMaterialWrap   wrap;
  AssetMaterialFilter filter;
  AssetMaterialAniso  aniso;
} AssetMaterialSampler;

ecs_comp_extern_public(AssetMaterialComp) {
  struct {
    EcsEntityId* assets;
    usize        count;
  } shaders;
  struct {
    AssetMaterialSampler* values;
    usize                 count;
  } samplers;
  AssetMaterialTopology   topology;
  AssetMaterialRasterizer rasterizer;
  u32                     lineWidth; // Line width (in pixels) when the rasterizer mode is 'lines'.
  AssetMaterialBlend      blend;
  AssetMaterialDepth      depth;
  AssetMaterialCull       cull;
};
