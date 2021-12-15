#include "asset_graphic.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "log_logger.h"

#include "canvas_internal.h"
#include "device_internal.h"
#include "graphic_internal.h"
#include "shader_internal.h"
#include "technique_internal.h"

typedef RvkShader* RvkShaderPtr;

static const char* rvk_to_null_term_scratch(String str) {
  const Mem scratchMem = alloc_alloc(g_alloc_scratch, str.size + 1, 1);
  mem_cpy(scratchMem, str);
  *mem_at_u8(scratchMem, str.size) = '\0';
  return scratchMem.ptr;
}

static String rvk_graphic_topology_str(const AssetGraphicTopology topology) {
  static const String msgs[] = {
      string_static("Triangles"),
      string_static("Lines"),
      string_static("LineStrip"),
  };
  ASSERT(array_elems(msgs) == AssetGraphicTopology_Count, "Incorrect number of names");
  return msgs[topology];
}

static String rvk_graphic_rasterizer_str(const AssetGraphicRasterizer rasterizer) {
  static const String msgs[] = {
      string_static("Fill"),
      string_static("Lines"),
      string_static("Points"),
  };
  ASSERT(array_elems(msgs) == AssetGraphicRasterizer_Count, "Incorrect number of names");
  return msgs[rasterizer];
}

static String rvk_graphic_blend_str(const AssetGraphicBlend blend) {
  static const String msgs[] = {
      string_static("None"),
      string_static("Alpha"),
      string_static("Additive"),
      string_static("AlphaAdditive"),
  };
  ASSERT(array_elems(msgs) == AssetGraphicBlend_Count, "Incorrect number of names");
  return msgs[blend];
}

static String rvk_graphic_depth_str(const AssetGraphicDepth depth) {
  static const String msgs[] = {
      string_static("None"),
      string_static("Less"),
      string_static("Always"),
  };
  ASSERT(array_elems(msgs) == AssetGraphicDepth_Count, "Incorrect number of names");
  return msgs[depth];
}

static String rvk_graphic_cull_str(const AssetGraphicCull cull) {
  static const String msgs[] = {
      string_static("Back"),
      string_static("Front"),
      string_static("None"),
  };
  ASSERT(array_elems(msgs) == AssetGraphicCull_Count, "Incorrect number of names");
  return msgs[cull];
}

static VkPipelineLayout rvk_pipeline_layout_create(const RvkGraphic* graphic) {
  const VkDescriptorSetLayout      descriptorLayouts[0]; // TODO: Support meshes / textures.
  const VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
      .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = array_elems(descriptorLayouts),
      .pSetLayouts    = descriptorLayouts,
  };
  VkPipelineLayout result;
  rvk_call(
      vkCreatePipelineLayout,
      graphic->dev->vkDev,
      &pipelineLayoutInfo,
      &graphic->dev->vkAlloc,
      &result);
  return result;
}

static VkPipelineShaderStageCreateInfo rvk_pipeline_shaderstage(const RvkShader* shader) {
  return (VkPipelineShaderStageCreateInfo){
      .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage  = shader->vkStage,
      .module = shader->vkModule,
      .pName  = rvk_to_null_term_scratch(shader->entryPoint),
  };
}

static VkPrimitiveTopology rvk_pipeline_input_topology(const RvkGraphic* graphic) {
  switch (graphic->topology) {
  case AssetGraphicTopology_Triangles:
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  case AssetGraphicTopology_Lines:
    return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  case AssetGraphicTopology_LineStrip:
    return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
  case AssetGraphicTopology_Count:
    break;
  }
  diag_crash();
}

static VkPolygonMode rvk_pipeline_polygonmode(RvkGraphic* graphic) {
  if (!graphic->dev->vkSupportedFeatures.fillModeNonSolid) {
    return VK_POLYGON_MODE_FILL;
  }
  switch (graphic->rasterizer) {
  case AssetGraphicRasterizer_Fill:
    return VK_POLYGON_MODE_FILL;
  case AssetGraphicRasterizer_Lines:
    return VK_POLYGON_MODE_LINE;
  case AssetGraphicRasterizer_Points:
    return VK_POLYGON_MODE_POINT;
  case AssetGraphicRasterizer_Count:
    break;
  }
  diag_crash();
}

static f32 rvk_pipeline_linewidth(RvkGraphic* graphic) {
  if (!graphic->dev->vkSupportedFeatures.wideLines) {
    return 1.0f;
  }
  return math_clamp_f32(
      graphic->lineWidth,
      graphic->dev->vkProperties.limits.lineWidthRange[0],
      graphic->dev->vkProperties.limits.lineWidthRange[1]);
}

static VkCullModeFlags rvk_pipeline_cullmode(RvkGraphic* graphic) {
  switch (graphic->cull) {
  case AssetGraphicCull_None:
    return VK_CULL_MODE_NONE;
  case AssetGraphicCull_Back:
    return VK_CULL_MODE_BACK_BIT;
  case AssetGraphicCull_Front:
    return VK_CULL_MODE_FRONT_BIT;
  case AssetGraphicCull_Count:
    break;
  }
  diag_crash();
}

static VkCompareOp rvk_pipeline_depth_compare(RvkGraphic* graphic) {
  switch (graphic->depth) {
  case AssetGraphicDepth_Less:
    // Use the 'greater' compare op, because we are using a reversed-z depthbuffer.
    return VK_COMPARE_OP_GREATER;
  case AssetGraphicDepth_Always:
    return VK_COMPARE_OP_ALWAYS;
  case AssetGraphicDepth_None:
    return VK_COMPARE_OP_NEVER;
  case AssetGraphicDepth_Count:
    break;
  }
  diag_crash();
}

static VkPipelineColorBlendAttachmentState rvk_pipeline_colorblend_attach(RvkGraphic* graphic) {
  const VkColorComponentFlags colorMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  switch (graphic->blend) {
  case AssetGraphicBlend_Alpha:
    return (VkPipelineColorBlendAttachmentState){
        .blendEnable         = true,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp        = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp        = VK_BLEND_OP_ADD,
        .colorWriteMask      = colorMask,
    };
  case AssetGraphicBlend_Additive:
    return (VkPipelineColorBlendAttachmentState){
        .blendEnable         = true,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .colorBlendOp        = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp        = VK_BLEND_OP_ADD,
        .colorWriteMask      = colorMask,
    };
  case AssetGraphicBlend_AlphaAdditive:
    return (VkPipelineColorBlendAttachmentState){
        .blendEnable         = true,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .colorBlendOp        = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp        = VK_BLEND_OP_ADD,
        .colorWriteMask      = colorMask,
    };
  case AssetGraphicBlend_None:
    return (VkPipelineColorBlendAttachmentState){0};
  case AssetGraphicBlend_Count:
    break;
  }
  diag_crash();
}

static VkPipeline
rvk_pipeline_create(RvkGraphic* graphic, VkPipelineLayout layout, const RvkCanvas* canvas) {

  VkPipelineShaderStageCreateInfo shaderStages[rvk_graphic_shaders_max];
  u32                             shaderStageCount = 0;
  array_for_t(graphic->shaders, RvkShaderPtr, itr) {
    if (*itr) {
      shaderStages[shaderStageCount++] = rvk_pipeline_shaderstage(*itr);
    }
  }

  const VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
  };
  const VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
      .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = rvk_pipeline_input_topology(graphic),
  };
  const VkViewport                        viewport      = {0};
  const VkRect2D                          scissor       = {0};
  const VkPipelineViewportStateCreateInfo viewportState = {
      .sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
      .viewportCount = 1,
      .pViewports    = &viewport,
      .scissorCount  = 1,
      .pScissors     = &scissor,
  };
  const VkPipelineRasterizationStateCreateInfo rasterizer = {
      .sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode = rvk_pipeline_polygonmode(graphic),
      .lineWidth   = rvk_pipeline_linewidth(graphic),
      .cullMode    = rvk_pipeline_cullmode(graphic),
      .frontFace   = VK_FRONT_FACE_CLOCKWISE,
  };
  const VkPipelineMultisampleStateCreateInfo multisampling = {
      .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
  };
  const VkPipelineDepthStencilStateCreateInfo depthStencil = {
      .sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthWriteEnable = true,
      .depthTestEnable  = graphic->depth != AssetGraphicDepth_None,
      .depthCompareOp   = rvk_pipeline_depth_compare(graphic),
  };
  const VkPipelineColorBlendAttachmentState colorBlendAttach =
      rvk_pipeline_colorblend_attach(graphic);
  const VkPipelineColorBlendStateCreateInfo colorBlending = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments    = &colorBlendAttach,
  };
  const VkDynamicState dynamicStates[] = {
      VK_DYNAMIC_STATE_VIEWPORT,
      VK_DYNAMIC_STATE_SCISSOR,
  };
  const VkPipelineDynamicStateCreateInfo dynamicStateInfo = {
      .sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
      .dynamicStateCount = array_elems(dynamicStates),
      .pDynamicStates    = dynamicStates,
  };
  const VkGraphicsPipelineCreateInfo pipelineInfo = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount          = shaderStageCount,
      .pStages             = shaderStages,
      .pVertexInputState   = &vertexInputInfo,
      .pInputAssemblyState = &inputAssembly,
      .pViewportState      = &viewportState,
      .pRasterizationState = &rasterizer,
      .pMultisampleState   = &multisampling,
      .pDepthStencilState  = &depthStencil,
      .pColorBlendState    = &colorBlending,
      .pDynamicState       = &dynamicStateInfo,
      .layout              = layout,
      .renderPass          = rvk_technique_vkrendpass(canvas->technique),
  };
  VkPipeline result;
  rvk_call(
      vkCreateGraphicsPipelines,
      graphic->dev->vkDev,
      null,
      1,
      &pipelineInfo,
      &graphic->dev->vkAlloc,
      &result);
  return result;
}

RvkGraphic* rvk_graphic_create(RvkDevice* dev, const AssetGraphicComp* asset) {
  RvkGraphic* graphic = alloc_alloc_t(g_alloc_heap, RvkGraphic);
  *graphic            = (RvkGraphic){
      .dev        = dev,
      .topology   = asset->topology,
      .rasterizer = asset->rasterizer,
      .lineWidth  = asset->lineWidth,
      .blend      = asset->blend,
      .depth      = asset->depth,
      .cull       = asset->cull,
  };

  log_d(
      "Vulkan graphic created",
      log_param("topology", fmt_text(rvk_graphic_topology_str(asset->topology))),
      log_param("rasterizer", fmt_text(rvk_graphic_rasterizer_str(asset->rasterizer))),
      log_param("lineWidth", fmt_float(asset->lineWidth)),
      log_param("blend", fmt_text(rvk_graphic_blend_str(asset->blend))),
      log_param("depth", fmt_text(rvk_graphic_depth_str(asset->depth))),
      log_param("cull", fmt_text(rvk_graphic_cull_str(asset->cull))));

  return graphic;
}

void rvk_graphic_destroy(RvkGraphic* graphic) {
  if (graphic->vkPipeline) {
    vkDestroyPipeline(graphic->dev->vkDev, graphic->vkPipeline, &graphic->dev->vkAlloc);
  }
  if (graphic->vkPipelineLayout) {
    vkDestroyPipelineLayout(graphic->dev->vkDev, graphic->vkPipelineLayout, &graphic->dev->vkAlloc);
  }

  log_d("Vulkan graphic destroyed");

  alloc_free_t(g_alloc_heap, graphic);
}

void rvk_graphic_shader_add(RvkGraphic* graphic, RvkShader* shader) {
  array_for_t(graphic->shaders, RvkShaderPtr, itr) {
    if (!*itr) {
      *itr = shader;
      return;
    }
  }
  diag_crash_msg("More then {} shaders are not supported", fmt_int(rvk_graphic_shaders_max));
}

bool rvk_graphic_prepare(RvkGraphic* graphic, const RvkCanvas* canvas) {
  if (graphic->vkPipeline) {
    // TODO: Validate that the already created pipeline is compatible with the given canvas.
    return true;
  }

  graphic->vkPipelineLayout = rvk_pipeline_layout_create(graphic);
  graphic->vkPipeline       = rvk_pipeline_create(graphic, graphic->vkPipelineLayout, canvas);
  return true;
}
