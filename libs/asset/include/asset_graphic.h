#pragma once
#include "data_registry.h"
#include "ecs_entity.h"
#include "ecs_module.h"

typedef enum {
  AssetGraphicTopology_Triangles,     // Separate triangles with 3 vertices.
  AssetGraphicTopology_TriangleStrip, // Form a strip of triangles (triangle connected to the last.
  AssetGraphicTopology_TriangleFan,   // Form a fan of triangles (every triangle has a common vert).
  AssetGraphicTopology_Lines,         // Separate lines with 2 vertices.
  AssetGraphicTopology_LineStrip,     // Form lines between all vertices.
  AssetGraphicTopology_Points,        // Every vertex is a treated as a point.

  AssetGraphicTopology_Count,
} AssetGraphicTopology;

typedef enum {
  AssetGraphicRasterizer_Fill,   // Fill the primitives with pixels.
  AssetGraphicRasterizer_Lines,  // Draw lines between the vertices.
  AssetGraphicRasterizer_Points, // Draw points on the vertices.

  AssetGraphicRasterizer_Count,
} AssetGraphicRasterizer;

typedef enum {
  AssetGraphicBlend_None,          // No blending, overwrite the attachment rgba.
  AssetGraphicBlend_Alpha,         // Blend based on alpha (attachment alpha is unchanged).
  AssetGraphicBlend_AlphaConstant, // Blend based on alpha (attachment alpha is set to constant).
  AssetGraphicBlend_Additive,      // Add the input to the attachment rgba.
  AssetGraphicBlend_PreMultiplied, // Multiply the attachment by the alpha and add the color's rgb.

  AssetGraphicBlend_Count,
} AssetGraphicBlend;

typedef enum {
  AssetGraphicWrap_Clamp,  // Use the edge pixel when sampling outside.
  AssetGraphicWrap_Repeat, // Repeat the texture when sampling outside.
  AssetGraphicWrap_Zero,   // Return zero when sampling outside.
} AssetGraphicWrap;

typedef enum {
  AssetGraphicFilter_Linear,  // Linearly blend between neighboring pixels.
  AssetGraphicFilter_Nearest, // Choose one of the pixels (sometimes known as 'point' filtering).
} AssetGraphicFilter;

typedef enum {
  AssetGraphicAniso_None, // No anisotropic filtering.
  AssetGraphicAniso_x2,   // Anisotropic filtering using 2 samples.
  AssetGraphicAniso_x4,   // Anisotropic filtering using 4 samples.
  AssetGraphicAniso_x8,   // Anisotropic filtering using 8 samples.
  AssetGraphicAniso_x16,  // Anisotropic filtering using 16 samples.
} AssetGraphicAniso;

typedef enum {
  AssetGraphicDepth_Less,               // Pass the depth-test if the fragment is closer.
  AssetGraphicDepth_LessOrEqual,        // Pass the depth-test if the fragment is closer or equal.
  AssetGraphicDepth_Equal,              // Pass the depth-test if the fragment is equal.
  AssetGraphicDepth_Greater,            // Pass the depth-test if the fragment is further away.
  AssetGraphicDepth_GreaterOrEqual,     // Pass the depth-test if the fragment is further or equal.
  AssetGraphicDepth_Always,             // Always pass the depth-test.
  AssetGraphicDepth_LessNoWrite,        // 'Less' without depth writing.
  AssetGraphicDepth_LessOrEqualNoWrite, // 'LessOrEqual' without depth writing.
  AssetGraphicDepth_EqualNoWrite,       // 'Equal' without depth writing.
  AssetGraphicDepth_GreaterNoWrite,     // 'Greater' without depth writing.
  AssetGraphicDepth_GreaterOrEqualNoWrite, // 'GreaterOrEqual' without depth writing.
  AssetGraphicDepth_AlwaysNoWrite,         // 'Always' without depth writing.

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
  u8     binding;
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
  bool               mipBlending; // Aka 'Trilinear' filtering.
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
  String                 meshId; // Mutually exclusive with 'vertexCount'.
  EcsEntityId            mesh;
  u32                    vertexCount; // Mutually exclusive with 'mesh'.
  i32                    renderOrder;
  AssetGraphicTopology   topology;
  AssetGraphicRasterizer rasterizer;
  u16                    lineWidth;  // Line width (in pixels) when the rasterizer mode is 'lines'.
  bool                   depthClamp; // Disables primitive z clipping.
  f32                    depthBiasConstant, depthBiasSlope;
  AssetGraphicBlend      blend;    // Blend mode for the primary attachment.
  AssetGraphicBlend      blendAux; // Blend mode for the other attachments.
  AssetGraphicDepth      depth;
  AssetGraphicCull       cull;

  /**
   * Usage of the blend-constant is blend-mode dependent:
   * - AssetGraphicBlend_Alpha:         Unused.
   * - AssetGraphicBlend_AlphaConstant: Controls the output alpha value.
   * - AssetGraphicBlend_Additive:      Unused.
   * - AssetGraphicBlend_PreMultiplied: Unused.
   */
  f32 blendConstant;
};

extern DataMeta g_assetGraphicMeta;
