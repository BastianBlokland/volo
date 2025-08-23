#pragma once
#include "asset/ref.h"
#include "core/array.h"
#include "data/registry.h"
#include "ecs/module.h"

typedef enum eAssetGraphicPass {
  AssetGraphicPass_None     = -1,
  AssetGraphicPass_Geometry = 0,
  AssetGraphicPass_Decal,
  AssetGraphicPass_Fog,
  AssetGraphicPass_FogBlur,
  AssetGraphicPass_Shadow,
  AssetGraphicPass_AmbientOcclusion,
  AssetGraphicPass_Forward,
  AssetGraphicPass_Distortion,
  AssetGraphicPass_Bloom,
  AssetGraphicPass_Post,

  AssetGraphicPass_Count,
} AssetGraphicPass;

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

typedef struct sAssetGraphicOverride {
  String name;
  u8     binding;
  f64    value;
} AssetGraphicOverride;

typedef struct {
  AssetRef program;
  HeapArray_t(AssetGraphicOverride) overrides;
} AssetGraphicShader;

typedef struct {
  AssetRef           texture;
  AssetGraphicWrap   wrap;
  AssetGraphicFilter filter;
  AssetGraphicAniso  anisotropy;
  bool               mipBlending; // Aka 'Trilinear' filtering.
} AssetGraphicSampler;

ecs_comp_extern_public(AssetGraphicComp) {
  AssetGraphicPass pass;
  u32              passRequirements; // (1 << AssetGraphicPass)[], mask of required passes.
  i32              passOrder;
  HeapArray_t(AssetGraphicShader) shaders;
  HeapArray_t(AssetGraphicSampler) samplers;
  AssetRef               mesh;        // Mutually exclusive with 'vertexCount'.
  u32                    vertexCount; // Mutually exclusive with 'mesh'.
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

extern DataMeta g_assetGraphicDefMeta;

/**
 * Find all asset references in the given graphic.
 */
u32 asset_graphic_refs(const AssetGraphicComp*, EcsEntityId out[], u32 outMax);

String asset_graphic_pass_name(AssetGraphicPass);
String asset_graphic_topology_name(AssetGraphicTopology);
String asset_graphic_rasterizer_name(AssetGraphicRasterizer);
String asset_graphic_blend_name(AssetGraphicBlend);
String asset_graphic_depth_name(AssetGraphicDepth);
String asset_graphic_cull_name(AssetGraphicCull);
