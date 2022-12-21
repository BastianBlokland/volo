#include "asset_graphic.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "log_logger.h"

#include "desc_internal.h"
#include "device_internal.h"
#include "graphic_internal.h"
#include "mesh_internal.h"
#include "pass_internal.h"
#include "repository_internal.h"
#include "sampler_internal.h"
#include "shader_internal.h"
#include "texture_internal.h"

typedef RvkShader* RvkShaderPtr;

static const u8 g_rendSupportedShaderSets[] = {
    RvkGraphicSet_Global,
    RvkGraphicSet_Graphic,
    RvkGraphicSet_Dynamic,
    RvkGraphicSet_Draw,
    RvkGraphicSet_Instance,
};

#define rend_uniform_buffer_mask (1 << RvkDescKind_UniformBufferDynamic)
#define rend_storage_buffer_mask (1 << RvkDescKind_StorageBuffer)
#define rend_image_sampler_2d_mask (1 << RvkDescKind_CombinedImageSampler2D)
#define rend_image_sampler_cube_mask (1 << RvkDescKind_CombinedImageSamplerCube)
#define rend_image_sampler_mask (rend_image_sampler_2d_mask | rend_image_sampler_cube_mask)

static const u32 g_rendSupportedGlobalBindings[rvk_desc_bindings_max] = {
    rend_uniform_buffer_mask,
    rend_image_sampler_2d_mask,
    rend_image_sampler_2d_mask,
    rend_image_sampler_2d_mask,
};

static const u32 g_rendSupportedGraphicBindings[rvk_desc_bindings_max] = {
    (rend_storage_buffer_mask | rend_image_sampler_mask),
    rend_image_sampler_mask,
    rend_image_sampler_mask,
    rend_image_sampler_mask,
    rend_image_sampler_mask,
    rend_image_sampler_mask,
    rend_image_sampler_mask,
};

static const u32 g_rendSupportedDynamicBindings[rvk_desc_bindings_max] = {
    rend_storage_buffer_mask,
};

static const u32 g_rendSupportedDrawBindings[rvk_desc_bindings_max] = {
    rend_uniform_buffer_mask,
};

static const u32 g_rendSupportedInstanceBindings[rvk_desc_bindings_max] = {
    rend_uniform_buffer_mask,
};

static bool rvk_desc_is_sampler(const RvkDescKind kind) {
  return kind == RvkDescKind_CombinedImageSampler2D || kind == RvkDescKind_CombinedImageSamplerCube;
}

static const char* rvk_to_null_term_scratch(String str) {
  const Mem scratchMem = alloc_alloc(g_alloc_scratch, str.size + 1, 1);
  mem_cpy(scratchMem, str);
  *mem_at_u8(scratchMem, str.size) = '\0';
  return scratchMem.ptr;
}

MAYBE_UNUSED static String rvk_graphic_topology_str(const AssetGraphicTopology topology) {
  static const String g_names[] = {
      string_static("Triangles"),
      string_static("TriangleStrip"),
      string_static("TriangleFan"),
      string_static("Lines"),
      string_static("LineStrip"),
      string_static("Points"),
  };
  ASSERT(array_elems(g_names) == AssetGraphicTopology_Count, "Incorrect number of names");
  return g_names[topology];
}

MAYBE_UNUSED static String rvk_graphic_rasterizer_str(const AssetGraphicRasterizer rasterizer) {
  static const String g_names[] = {
      string_static("Fill"),
      string_static("Lines"),
      string_static("Points"),
  };
  ASSERT(array_elems(g_names) == AssetGraphicRasterizer_Count, "Incorrect number of names");
  return g_names[rasterizer];
}

MAYBE_UNUSED static String rvk_graphic_blend_str(const AssetGraphicBlend blend) {
  static const String g_names[] = {
      string_static("None"),
      string_static("Alpha"),
      string_static("Additive"),
      string_static("AlphaAdditive"),
      string_static("PreMultiplied"),
  };
  ASSERT(array_elems(g_names) == AssetGraphicBlend_Count, "Incorrect number of names");
  return g_names[blend];
}

MAYBE_UNUSED static String rvk_graphic_depth_str(const AssetGraphicDepth depth) {
  static const String g_names[] = {
      string_static("Less"),
      string_static("LessOrEqual"),
      string_static("Equal"),
      string_static("Always"),
      string_static("LessNoWrite"),
      string_static("LessOrEqualNoWrite"),
      string_static("EqualNoWrite"),
      string_static("AlwaysNoWrite"),
  };
  ASSERT(array_elems(g_names) == AssetGraphicDepth_Count, "Incorrect number of names");
  return g_names[depth];
}

MAYBE_UNUSED static String rvk_graphic_cull_str(const AssetGraphicCull cull) {
  static const String g_names[] = {
      string_static("Back"),
      string_static("Front"),
      string_static("None"),
  };
  ASSERT(array_elems(g_names) == AssetGraphicCull_Count, "Incorrect number of names");
  return g_names[cull];
}

static RvkSamplerWrap rvk_graphic_wrap(const AssetGraphicWrap assetWrap) {
  switch (assetWrap) {
  case AssetGraphicWrap_Repeat:
    return RvkSamplerWrap_Repeat;
  case AssetGraphicWrap_Clamp:
    return RvkSamplerWrap_Clamp;
  }
  diag_crash();
}

static RvkSamplerFilter rvk_graphic_filter(const AssetGraphicFilter assetFilter) {
  switch (assetFilter) {
  case AssetGraphicFilter_Linear:
    return RvkSamplerFilter_Linear;
  case AssetGraphicFilter_Nearest:
    return RvkSamplerFilter_Nearest;
  }
  diag_crash();
}

static RvkSamplerAniso rvk_graphic_aniso(const AssetGraphicAniso assetAniso) {
  switch (assetAniso) {
  case AssetGraphicAniso_None:
    return RvkSamplerAniso_None;
  case AssetGraphicAniso_x2:
    return RvkSamplerAniso_x2;
  case AssetGraphicAniso_x4:
    return RvkSamplerAniso_x4;
  case AssetGraphicAniso_x8:
    return RvkSamplerAniso_x8;
  case AssetGraphicAniso_x16:
    return RvkSamplerAniso_x16;
  }
  diag_crash();
}

static bool rvk_graphic_desc_merge(RvkDescMeta* meta, const RvkDescMeta* other) {
  for (usize i = 0; i != rvk_desc_bindings_max; ++i) {
    if (meta->bindings[i] && other->bindings[i]) {
      if (UNLIKELY(meta->bindings[i] != other->bindings[i])) {
        log_e(
            "Incompatible shader descriptor binding {}",
            log_param("binding", fmt_int(i)),
            log_param("a", fmt_text(rvk_desc_kind_str(meta->bindings[i]))),
            log_param("b", fmt_text(rvk_desc_kind_str(other->bindings[i]))));
        return false;
      }
    } else if (other->bindings[i]) {
      meta->bindings[i] = other->bindings[i];
    }
  }
  return true;
}

static RvkDescMeta rvk_graphic_desc_meta(RvkGraphic* graphic, const usize set) {
  RvkDescMeta meta = {0};
  array_for_t(graphic->shaders, RvkGraphicShader, itr) {
    if (itr->shader) {
      if (UNLIKELY(!rvk_graphic_desc_merge(&meta, &itr->shader->descriptors[set]))) {
        graphic->flags |= RvkGraphicFlags_Invalid;
      }
    }
  }
  return meta;
}

static VkPipelineLayout rvk_pipeline_layout_create(const RvkGraphic* graphic, const RvkPass* pass) {
  const RvkDescMeta           globalDescMeta      = rvk_pass_meta_global(pass);
  const RvkDescMeta           dynamicDescMeta     = rvk_pass_meta_dynamic(pass);
  const RvkDescMeta           drawDescMeta        = rvk_pass_meta_draw(pass);
  const RvkDescMeta           instanceDescMeta    = rvk_pass_meta_instance(pass);
  const VkDescriptorSetLayout descriptorLayouts[] = {
      rvk_desc_vklayout(graphic->device->descPool, &globalDescMeta),
      rvk_desc_set_vklayout(graphic->descSet),
      rvk_desc_vklayout(graphic->device->descPool, &dynamicDescMeta),
      rvk_desc_vklayout(graphic->device->descPool, &drawDescMeta),
      rvk_desc_vklayout(graphic->device->descPool, &instanceDescMeta),
  };
  const VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
      .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = array_elems(descriptorLayouts),
      .pSetLayouts    = descriptorLayouts,
  };
  RvkDevice*       dev = graphic->device;
  VkPipelineLayout result;
  rvk_call(vkCreatePipelineLayout, dev->vkDev, &pipelineLayoutInfo, &dev->vkAlloc, &result);
  return result;
}

static VkPipelineShaderStageCreateInfo rvk_pipeline_shader(const RvkGraphicShader* graphicShader) {

  VkSpecializationInfo* specialization = alloc_alloc_t(g_alloc_scratch, VkSpecializationInfo);
  *specialization                      = rvk_shader_specialize_scratch(
      graphicShader->shader, graphicShader->overrides.values, graphicShader->overrides.count);

  return (VkPipelineShaderStageCreateInfo){
      .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage               = graphicShader->shader->vkStage,
      .module              = graphicShader->shader->vkModule,
      .pName               = rvk_to_null_term_scratch(graphicShader->shader->entryPoint),
      .pSpecializationInfo = specialization,
  };
}

static VkPrimitiveTopology rvk_pipeline_input_topology(const RvkGraphic* graphic) {
  switch (graphic->topology) {
  case AssetGraphicTopology_Triangles:
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  case AssetGraphicTopology_TriangleStrip:
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
  case AssetGraphicTopology_TriangleFan:
    return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
  case AssetGraphicTopology_Lines:
    return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  case AssetGraphicTopology_LineStrip:
    return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
  case AssetGraphicTopology_Points:
    return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
  case AssetGraphicTopology_Count:
    break;
  }
  diag_crash();
}

static VkPolygonMode rvk_pipeline_polygonmode(RvkGraphic* graphic) {
  if (!(graphic->device->flags & RvkDeviceFlags_SupportFillNonSolid)) {
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
  RvkDevice* dev = graphic->device;
  if (!(dev->flags & RvkDeviceFlags_SupportWideLines)) {
    return 1.0f;
  }
  return math_clamp_f32(
      graphic->lineWidth ? graphic->lineWidth : 1.0f,
      dev->vkProperties.limits.lineWidthRange[0],
      dev->vkProperties.limits.lineWidthRange[1]);
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
  case AssetGraphicDepth_LessNoWrite:
    // Use the 'greater' compare op, because we are using a reversed-z depthbuffer.
    return VK_COMPARE_OP_GREATER;
  case AssetGraphicDepth_Equal:
    return VK_COMPARE_OP_EQUAL;
  case AssetGraphicDepth_LessOrEqual:
  case AssetGraphicDepth_LessOrEqualNoWrite:
    return VK_COMPARE_OP_GREATER_OR_EQUAL;
  case AssetGraphicDepth_EqualNoWrite:
    return VK_COMPARE_OP_EQUAL;
  case AssetGraphicDepth_Always:
  case AssetGraphicDepth_AlwaysNoWrite:
    return VK_COMPARE_OP_ALWAYS;
  case AssetGraphicDepth_Count:
    break;
  }
  diag_crash();
}

static bool rvk_pipeline_depth_write(RvkGraphic* graphic) {
  switch (graphic->depth) {
  case AssetGraphicDepth_Less:
  case AssetGraphicDepth_LessOrEqual:
  case AssetGraphicDepth_Equal:
  case AssetGraphicDepth_Always:
    return true;
  case AssetGraphicDepth_LessNoWrite:
  case AssetGraphicDepth_LessOrEqualNoWrite:
  case AssetGraphicDepth_EqualNoWrite:
  case AssetGraphicDepth_AlwaysNoWrite:
    return false;
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
  case AssetGraphicBlend_PreMultiplied:
    return (VkPipelineColorBlendAttachmentState){
        .blendEnable         = true,
        .srcColorBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp        = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .alphaBlendOp        = VK_BLEND_OP_ADD,
        .colorWriteMask      = colorMask,
    };
  case AssetGraphicBlend_None:
    return (VkPipelineColorBlendAttachmentState){
        .colorWriteMask = colorMask,
    };
  case AssetGraphicBlend_Count:
    break;
  }
  diag_crash();
}

static VkPipeline
rvk_pipeline_create(RvkGraphic* graphic, const VkPipelineLayout layout, const RvkPass* pass) {

  VkPipelineShaderStageCreateInfo shaderStages[rvk_graphic_shaders_max];
  u32                             shaderStageCount = 0;
  array_for_t(graphic->shaders, RvkGraphicShader, itr) {
    if (itr->shader) {
      shaderStages[shaderStageCount++] = rvk_pipeline_shader(itr);
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
      .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode             = rvk_pipeline_polygonmode(graphic),
      .lineWidth               = rvk_pipeline_linewidth(graphic),
      .cullMode                = rvk_pipeline_cullmode(graphic),
      .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .depthBiasEnable         = graphic->depthBias < -1e-6 || graphic->depthBias > 1e-6,
      .depthBiasConstantFactor = graphic->depthBias,
  };

  const VkPipelineMultisampleStateCreateInfo multisampling = {
      .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
  };

  const VkPipelineDepthStencilStateCreateInfo depthStencil = {
      .sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthWriteEnable = rvk_pipeline_depth_write(graphic),
      .depthTestEnable  = graphic->depth != AssetGraphicDepth_Always &&
                         graphic->depth != AssetGraphicDepth_AlwaysNoWrite,
      .depthCompareOp = rvk_pipeline_depth_compare(graphic),
  };

  const u32                           colorAttachmentCount = 32 - bits_clz_32(graphic->outputMask);
  VkPipelineColorBlendAttachmentState colorBlends[16];
  // TODO: Validate that the output at index 0 is actually used.
  colorBlends[0] = rvk_pipeline_colorblend_attach(graphic);
  for (u32 i = 1; i < colorAttachmentCount; ++i) {
    // NOTE: No blending for the other color attachments.
    colorBlends[i] = (VkPipelineColorBlendAttachmentState){
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
    };
  }
  const VkPipelineColorBlendStateCreateInfo colorBlending = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = colorAttachmentCount,
      .pAttachments    = colorBlends,
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
      .renderPass          = rvk_pass_vkrenderpass(pass),
  };
  RvkDevice*      dev  = graphic->device;
  VkPipelineCache psoc = dev->vkPipelineCache;
  VkPipeline      result;
  rvk_call(vkCreateGraphicsPipelines, dev->vkDev, psoc, 1, &pipelineInfo, &dev->vkAlloc, &result);
  return result;
}

static void rvk_graphic_set_missing_sampler(
    RvkGraphic* graphic, const u32 samplerIndex, const RvkDescKind kind) {
  diag_assert(!graphic->samplers[samplerIndex].texture);

  const RvkRepositoryId repoId = kind == RvkDescKind_CombinedImageSamplerCube
                                     ? RvkRepositoryId_MissingTextureCube
                                     : RvkRepositoryId_MissingTexture;

  RvkDevice*  dev = graphic->device;
  RvkTexture* tex = rvk_repository_texture_get(dev->repository, repoId);

  graphic->samplers[samplerIndex].texture = tex;
  graphic->samplers[samplerIndex].sampler = rvk_sampler_create(
      dev,
      RvkSamplerWrap_Repeat,
      RvkSamplerFilter_Nearest,
      RvkSamplerAniso_None,
      tex->image.mipLevels);
}

static bool rvk_graphic_validate_shaders(const RvkGraphic* graphic) {
  VkShaderStageFlagBits foundStages = 0;
  u16                   vertexShaderOutputs, fragmentShaderInputs;
  array_for_t(graphic->shaders, RvkGraphicShader, itr) {
    if (itr->shader) {
      // Validate stage.
      if (foundStages & itr->shader->vkStage) {
        log_e("Duplicate shader stage", log_param("graphic", fmt_text(graphic->dbgName)));
        return false;
      }
      foundStages |= itr->shader->vkStage;

      if (itr->shader->vkStage == VK_SHADER_STAGE_VERTEX_BIT) {
        vertexShaderOutputs = itr->shader->outputMask;
      } else if (itr->shader->vkStage == VK_SHADER_STAGE_FRAGMENT_BIT) {
        fragmentShaderInputs = itr->shader->inputMask;
      }

      // Validate used sets.
      for (u32 set = 0; set != rvk_shader_desc_max; ++set) {
        const bool supported = mem_contains(mem_var(g_rendSupportedShaderSets), set);
        if (!supported && rvk_shader_set_used(itr->shader, set)) {
          log_e(
              "Shader uses unsupported set",
              log_param("graphic", fmt_text(graphic->dbgName)),
              log_param("set", fmt_int(set)));
          return false;
        }
      }
    }
  }
  if (!(foundStages & VK_SHADER_STAGE_VERTEX_BIT)) {
    log_e("Vertex shader missing", log_param("graphic", fmt_text(graphic->dbgName)));
    return false;
  }
  if (!(foundStages & VK_SHADER_STAGE_FRAGMENT_BIT)) {
    log_e("Vertex shader missing", log_param("graphic", fmt_text(graphic->dbgName)));
    return false;
  }
  if (vertexShaderOutputs != fragmentShaderInputs) {
    log_e(
        "Vertex shader outputs do not match fragment inputs",
        log_param("graphic", fmt_text(graphic->dbgName)));
    return false;
  }
  return true;
}

static u16 rvk_graphic_output_mask(const RvkGraphic* graphic) {
  array_for_t(graphic->shaders, RvkGraphicShader, itr) {
    if (itr->shader && itr->shader->vkStage == VK_SHADER_STAGE_FRAGMENT_BIT) {
      return itr->shader->outputMask;
    }
  }
  return 0;
}

static bool rend_graphic_validate_set(
    const RvkGraphic*  graphic,
    const u32          set,
    const RvkDescMeta* setBindings,
    const u32          supportedKinds[rvk_desc_bindings_max]) {

  for (u32 binding = 0; binding != rvk_desc_bindings_max; binding++) {
    const BitSet      supportedBits = bitset_from_var(supportedKinds[binding]);
    const RvkDescKind boundKind     = setBindings->bindings[binding];
    if (UNLIKELY(boundKind && !bitset_test(supportedBits, boundKind))) {

      // Gather a list of the supported kinds.
      FormatArg supported[16]  = {0};
      usize     supportedCount = 0;
      bitset_for(supportedBits, supportedKind) {
        supported[supportedCount++] = fmt_text(rvk_desc_kind_str((RvkDescKind)supportedKind));
      }
      log_e(
          "Unsupported shader binding",
          log_param("graphic", fmt_text(graphic->dbgName)),
          log_param("set", fmt_int(set)),
          log_param("binding", fmt_int(binding)),
          log_param("found", fmt_text(rvk_desc_kind_str(setBindings->bindings[binding]))),
          log_param("supported", fmt_list(supported)));
      return false;
    }
  }
  return true;
}

RvkGraphic*
rvk_graphic_create(RvkDevice* dev, const AssetGraphicComp* asset, const String dbgName) {
  RvkGraphic* graphic = alloc_alloc_t(g_alloc_heap, RvkGraphic);

  *graphic = (RvkGraphic){
      .device      = dev,
      .dbgName     = string_dup(g_alloc_heap, dbgName),
      .topology    = asset->topology,
      .rasterizer  = asset->rasterizer,
      .lineWidth   = asset->lineWidth,
      .depthBias   = asset->depthBias,
      .renderOrder = asset->renderOrder,
      .blend       = asset->blend,
      .depth       = asset->depth,
      .cull        = asset->cull,
      .vertexCount = asset->vertexCount,
  };

  log_d(
      "Vulkan graphic created",
      log_param("name", fmt_text(dbgName)),
      log_param("topology", fmt_text(rvk_graphic_topology_str(asset->topology))),
      log_param("rasterizer", fmt_text(rvk_graphic_rasterizer_str(asset->rasterizer))),
      log_param("line-width", fmt_int(asset->lineWidth)),
      log_param("depth-bias", fmt_float(asset->depthBias)),
      log_param("blend", fmt_text(rvk_graphic_blend_str(asset->blend))),
      log_param("depth", fmt_text(rvk_graphic_depth_str(asset->depth))),
      log_param("cull", fmt_text(rvk_graphic_cull_str(asset->cull))));

  return graphic;
}

void rvk_graphic_destroy(RvkGraphic* graphic) {
  RvkDevice* dev = graphic->device;
  if (graphic->vkPipeline) {
    vkDestroyPipeline(dev->vkDev, graphic->vkPipeline, &dev->vkAlloc);
  }
  if (graphic->vkPipelineLayout) {
    vkDestroyPipelineLayout(dev->vkDev, graphic->vkPipelineLayout, &dev->vkAlloc);
  }
  if (rvk_desc_valid(graphic->descSet)) {
    rvk_desc_free(graphic->descSet);
  }
  array_for_t(graphic->shaders, RvkGraphicShader, itr) {
    if (itr->overrides.count) {
      array_ptr_for_t(itr->overrides, RvkShaderOverride, override) {
        string_free(g_alloc_heap, override->name);
      }
      alloc_free_array_t(g_alloc_heap, itr->overrides.values, itr->overrides.count);
    }
  }
  array_for_t(graphic->samplers, RvkGraphicSampler, itr) {
    if (itr->texture) {
      rvk_sampler_destroy(&itr->sampler, dev);
    }
  }

  log_d("Vulkan graphic destroyed", log_param("name", fmt_text(graphic->dbgName)));

  string_free(g_alloc_heap, graphic->dbgName);
  alloc_free_t(g_alloc_heap, graphic);
}

void rvk_graphic_shader_add(
    RvkGraphic* graphic, RvkShader* shader, AssetGraphicOverride* overrides, usize overrideCount) {

  array_for_t(graphic->shaders, RvkGraphicShader, itr) {
    if (!itr->shader) {
      itr->shader = shader;

      if (overrideCount) {
        itr->overrides.values = alloc_array_t(g_alloc_heap, RvkShaderOverride, overrideCount);
        itr->overrides.count  = overrideCount;
        for (usize i = 0; i != overrideCount; ++i) {
          itr->overrides.values[i] = (RvkShaderOverride){
              .binding = overrides[i].binding,
              .name    = string_dup(g_alloc_heap, overrides[i].name),
              .value   = overrides[i].value,
          };
        }
      }
      return;
    }
  }
  log_e(
      "Shaders limit exceeded",
      log_param("graphic", fmt_text(graphic->dbgName)),
      log_param("limit", fmt_int(rvk_graphic_shaders_max)));
}

void rvk_graphic_mesh_add(RvkGraphic* graphic, RvkMesh* mesh) {
  diag_assert_msg(!graphic->mesh, "Only a single mesh per graphic supported");
  graphic->mesh = mesh;
}

void rvk_graphic_sampler_add(
    RvkGraphic* graphic, RvkTexture* texture, const AssetGraphicSampler* sampler) {

  RvkDevice* dev = graphic->device;
  array_for_t(graphic->samplers, RvkGraphicSampler, itr) {
    if (!itr->texture) {
      const RvkSamplerWrap   wrap       = rvk_graphic_wrap(sampler->wrap);
      const RvkSamplerFilter filter     = rvk_graphic_filter(sampler->filter);
      const RvkSamplerAniso  anisotropy = rvk_graphic_aniso(sampler->anisotropy);
      itr->texture                      = texture;
      itr->sampler = rvk_sampler_create(dev, wrap, filter, anisotropy, texture->image.mipLevels);

      rvk_debug_name_sampler(
          graphic->device->debug, itr->sampler.vkSampler, "{}", fmt_text(texture->dbgName));
      return;
    }
  }
  log_e(
      "Sampler limit exceeded",
      log_param("graphic", fmt_text(graphic->dbgName)),
      log_param("limit", fmt_int(rvk_graphic_samplers_max)));
}

bool rvk_graphic_prepare(RvkGraphic* graphic, VkCommandBuffer vkCmdBuf, const RvkPass* pass) {
  diag_assert_msg(!rvk_pass_active(pass), "Pass already active");
  if (UNLIKELY(graphic->flags & RvkGraphicFlags_Invalid)) {
    return false;
  }
  if (!graphic->vkPipeline) {
    if (UNLIKELY(!rvk_graphic_validate_shaders(graphic))) {
      graphic->flags |= RvkGraphicFlags_Invalid;
    }
    graphic->outputMask = rvk_graphic_output_mask(graphic);

    // Prepare global set bindings.
    const RvkDescMeta globalDescMeta = rvk_graphic_desc_meta(graphic, RvkGraphicSet_Global);
    if (UNLIKELY(!rend_graphic_validate_set(
            graphic, RvkGraphicSet_Global, &globalDescMeta, g_rendSupportedGlobalBindings))) {
      graphic->flags |= RvkGraphicFlags_Invalid;
    }
    for (u16 i = 0; i != rvk_desc_bindings_max; ++i) {
      if (globalDescMeta.bindings[i]) {
        graphic->globalBindings |= 1 << i;
      }
    }

    // Prepare dynamic bindings.
    const RvkDescMeta dynamicDescMeta = rvk_graphic_desc_meta(graphic, RvkGraphicSet_Dynamic);
    if (UNLIKELY(!rend_graphic_validate_set(
            graphic, RvkGraphicSet_Dynamic, &dynamicDescMeta, g_rendSupportedDynamicBindings))) {
      graphic->flags |= RvkGraphicFlags_Invalid;
    }
    if (dynamicDescMeta.bindings[0] == RvkDescKind_StorageBuffer) {
      graphic->flags |= RvkGraphicFlags_RequireDynamicMesh;
    }

    // Prepare draw set bindings.
    const RvkDescMeta drawDescMeta = rvk_graphic_desc_meta(graphic, RvkGraphicSet_Draw);
    if (UNLIKELY(!rend_graphic_validate_set(
            graphic, RvkGraphicSet_Draw, &drawDescMeta, g_rendSupportedDrawBindings))) {
      graphic->flags |= RvkGraphicFlags_Invalid;
    }
    if (drawDescMeta.bindings[0] == RvkDescKind_UniformBufferDynamic) {
      graphic->flags |= RvkGraphicFlags_RequireDrawData;
    }

    // Prepare instance set bindings.
    const RvkDescMeta instanceDescMeta = rvk_graphic_desc_meta(graphic, RvkGraphicSet_Instance);
    if (UNLIKELY(!rend_graphic_validate_set(
            graphic, RvkGraphicSet_Instance, &instanceDescMeta, g_rendSupportedInstanceBindings))) {
      graphic->flags |= RvkGraphicFlags_Invalid;
    }
    if (instanceDescMeta.bindings[0] == RvkDescKind_UniformBufferDynamic) {
      graphic->flags |= RvkGraphicFlags_RequireInstanceData;
    }

    // Prepare graphic set bindings.
    const RvkDescMeta graphicDescMeta = rvk_graphic_desc_meta(graphic, RvkGraphicSet_Graphic);
    if (UNLIKELY(!rend_graphic_validate_set(
            graphic, RvkGraphicSet_Graphic, &graphicDescMeta, g_rendSupportedGraphicBindings))) {
      graphic->flags |= RvkGraphicFlags_Invalid;
    }
    graphic->descSet = rvk_desc_alloc(graphic->device->descPool, &graphicDescMeta);

    // Attach mesh.
    if (graphicDescMeta.bindings[0] == RvkDescKind_StorageBuffer) {
      if (LIKELY(graphic->mesh)) {
        rvk_desc_set_attach_buffer(graphic->descSet, 0, &graphic->mesh->vertexBuffer, 0);
      } else {
        log_e("Shader requires a mesh", log_param("graphic", fmt_text(graphic->dbgName)));
        graphic->flags |= RvkGraphicFlags_Invalid;
      }
    }
    if (UNLIKELY(graphic->mesh && graphic->flags & RvkGraphicFlags_RequireDynamicMesh)) {
      log_e(
          "Graphic cannot use both a normal and a dynamic mesh ",
          log_param("graphic", fmt_text(graphic->dbgName)));
      graphic->flags |= RvkGraphicFlags_Invalid;
    }

    // Attach samplers.
    u32 samplerIndex = 0;
    for (u32 i = 0; i != rvk_desc_bindings_max; ++i) {
      const RvkDescKind kind = graphicDescMeta.bindings[i];
      if (rvk_desc_is_sampler(kind)) {
        if (UNLIKELY(samplerIndex == rvk_graphic_samplers_max)) {
          log_e(
              "Shader exceeds texture limit",
              log_param("graphic", fmt_text(graphic->dbgName)),
              log_param("limit", fmt_int(rvk_graphic_samplers_max)));
          graphic->flags |= RvkGraphicFlags_Invalid;
          break;
        }
        if (!graphic->samplers[samplerIndex].texture) {
          rvk_graphic_set_missing_sampler(graphic, samplerIndex, kind);
        }
        if (kind != rvk_texture_sampler_kind(graphic->samplers[samplerIndex].texture)) {
          log_e(
              "Mismatched shader texture sampler kind",
              log_param("graphic", fmt_text(graphic->dbgName)),
              log_param("sampler", fmt_int(samplerIndex)),
              log_param("expected", fmt_text(rvk_desc_kind_str(kind))));
          graphic->flags |= RvkGraphicFlags_Invalid;
          break;
        }
        const RvkImage*   image   = &graphic->samplers[samplerIndex].texture->image;
        const RvkSampler* sampler = &graphic->samplers[samplerIndex].sampler;
        rvk_desc_set_attach_sampler(graphic->descSet, i, image, sampler);
        ++samplerIndex;
      }
    }

    if (graphic->flags & RvkGraphicFlags_Invalid) {
      return false;
    }

    graphic->vkPipelineLayout = rvk_pipeline_layout_create(graphic, pass);
    graphic->vkPipeline       = rvk_pipeline_create(graphic, graphic->vkPipelineLayout, pass);

    rvk_debug_name_pipeline_layout(
        graphic->device->debug, graphic->vkPipelineLayout, "{}", fmt_text(graphic->dbgName));
    rvk_debug_name_pipeline(
        graphic->device->debug, graphic->vkPipeline, "{}", fmt_text(graphic->dbgName));
  }
  if (graphic->mesh && !rvk_mesh_prepare(graphic->mesh)) {
    return false;
  }
  array_for_t(graphic->samplers, RvkGraphicSampler, itr) {
    if (itr->texture && !rvk_texture_prepare(itr->texture, vkCmdBuf)) {
      return false;
    }
  }
  graphic->flags |= RvkGraphicFlags_Ready;
  return true;
}

void rvk_graphic_bind(const RvkGraphic* graphic, VkCommandBuffer vkCmdBuf) {
  diag_assert_msg(graphic->flags & RvkGraphicFlags_Ready, "Graphic is not ready");

  vkCmdBindPipeline(vkCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, graphic->vkPipeline);

  const VkDescriptorSet vkGraphicDescSet = rvk_desc_set_vkset(graphic->descSet);
  vkCmdBindDescriptorSets(
      vkCmdBuf,
      VK_PIPELINE_BIND_POINT_GRAPHICS,
      graphic->vkPipelineLayout,
      RvkGraphicSet_Graphic,
      1,
      &vkGraphicDescSet,
      0,
      null);

  if (graphic->mesh) {
    vkCmdBindIndexBuffer(
        vkCmdBuf,
        graphic->mesh->indexBuffer.vkBuffer,
        0,
        sizeof(AssetMeshIndex) == 2 ? VK_INDEX_TYPE_UINT16 : VK_INDEX_TYPE_UINT32);
  }
}
