#pragma once
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  AssetGfxTopology_Triangles, // Separate triangles with 3 vertices.
  AssetGfxTopology_Lines,     // Separate lines with 2 vertices.
  AssetGfxTopology_LineStrip, // Form lines between all vertices.
} AssetGfxTopology;

typedef enum {
  AssetGfxRasterizer_Fill,   // Fill the primitives with pixels.
  AssetGfxRasterizer_Lines,  // Draw lines between the vertices.
  AssetGfxRasterizer_Points, // Draw points on the vertices.
} AssetGfxRasterizer;

typedef enum {
  AssetGfxBlend_None,          // No blending, just replace the framebuffer's rgb values.
  AssetGfxBlend_Alpha,         // Blend between rgb and the framebuffer based on the alpha.
  AssetGfxBlend_Additive,      // Add rgb to the framebuffer (ignores alpha).
  AssetGfxBlend_AlphaAdditive, // Multiply rgb by alpha and add them to the framebuffer.
} AssetGfxBlend;

typedef enum {
  AssetGfxWrap_Repeat, // Repeat the texture when sampling outside.
  AssetGfxWrap_Clamp,  // Use the edge pixel when sampling outside.
} AssetGfxWrap;

typedef enum {
  AssetGfxFilter_Linear,  // Choose one of the pixels (sometimes known as 'point' filtering).
  AssetGfxFilter_Nearest, // Linearly blend between neighboring pixels.
} AssetGfxFilter;

typedef enum {
  AssetGfxAniso_None, // No anisotropic filtering.
  AssetGfxAniso_x2,   // Anisotropic filtering using 2 samples.
  AssetGfxAniso_x4,   // Anisotropic filtering using 4 samples.
  AssetGfxAniso_x8,   // Anisotropic filtering using 8 samples.
  AssetGfxAniso_x16,  // Anisotropic filtering using 16 samples.
} AssetGfxAniso;

typedef enum {
  AssetGfxDepth_None,   // No depth-testing.
  AssetGfxDepth_Less,   // Pass the depth-test if the fragment is closer.
  AssetGfxDepth_Always, // Always pass the depth-test.
} AssetGfxDepth;

typedef enum {
  AssetGfxCull_Back,  // Cull back-facing primitives.
  AssetGfxCull_Front, // Cull front-facing primitives.
  AssetGfxCull_None,  // No culling.
} AssetGfxCull;

typedef struct {
  String         textureId;
  EcsEntityId    texture;
  AssetGfxWrap   wrap;
  AssetGfxFilter filter;
  AssetGfxAniso  anisotropy;
} AssetGfxSampler;

typedef struct {
  String      shaderId;
  EcsEntityId shader;
} AssetGfxShader;

ecs_comp_extern_public(AssetGfxComp) {
  struct {
    AssetGfxShader* values;
    usize           count;
  } shaders;
  struct {
    AssetGfxSampler* values;
    usize            count;
  } samplers;
  String             meshId;
  EcsEntityId        mesh;
  AssetGfxTopology   topology;
  AssetGfxRasterizer rasterizer;
  u32                lineWidth; // Line width (in pixels) when the rasterizer mode is 'lines'.
  AssetGfxBlend      blend;
  AssetGfxDepth      depth;
  AssetGfxCull       cull;
};
