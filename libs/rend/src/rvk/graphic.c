#include "asset_graphic.h"
#include "core_alloc.h"
#include "core_array.h"
#include "core_diag.h"
#include "core_math.h"
#include "log_logger.h"

#include "desc_internal.h"
#include "device_internal.h"
#include "graphic_internal.h"
#include "mesh_internal.h"
#include "repository_internal.h"
#include "sampler_internal.h"
#include "shader_internal.h"
#include "texture_internal.h"

typedef RvkShader* RvkShaderPtr;

#define rvk_desc_global_set 0
#define rvk_desc_graphic_set 1
#define rvk_desc_graphic_bind_mesh 0
#define rvk_desc_instance_set 2

static const char* rvk_to_null_term_scratch(String str) {
  const Mem scratchMem = alloc_alloc(g_alloc_scratch, str.size + 1, 1);
  mem_cpy(scratchMem, str);
  *mem_at_u8(scratchMem, str.size) = '\0';
  return scratchMem.ptr;
}

static String rvk_graphic_topology_str(const AssetGraphicTopology topology) {
  static const String names[] = {
      string_static("Triangles"),
      string_static("Lines"),
      string_static("LineStrip"),
  };
  ASSERT(array_elems(names) == AssetGraphicTopology_Count, "Incorrect number of names");
  return names[topology];
}

static String rvk_graphic_rasterizer_str(const AssetGraphicRasterizer rasterizer) {
  static const String names[] = {
      string_static("Fill"),
      string_static("Lines"),
      string_static("Points"),
  };
  ASSERT(array_elems(names) == AssetGraphicRasterizer_Count, "Incorrect number of names");
  return names[rasterizer];
}

static String rvk_graphic_blend_str(const AssetGraphicBlend blend) {
  static const String names[] = {
      string_static("None"),
      string_static("Alpha"),
      string_static("Additive"),
      string_static("AlphaAdditive"),
  };
  ASSERT(array_elems(names) == AssetGraphicBlend_Count, "Incorrect number of names");
  return names[blend];
}

static String rvk_graphic_depth_str(const AssetGraphicDepth depth) {
  static const String names[] = {
      string_static("None"),
      string_static("Less"),
      string_static("Always"),
  };
  ASSERT(array_elems(names) == AssetGraphicDepth_Count, "Incorrect number of names");
  return names[depth];
}

static String rvk_graphic_cull_str(const AssetGraphicCull cull) {
  static const String names[] = {
      string_static("Back"),
      string_static("Front"),
      string_static("None"),
  };
  ASSERT(array_elems(names) == AssetGraphicCull_Count, "Incorrect number of names");
  return names[cull];
}

static RvkSamplerWrap rvk_graphic_wrap(const AssetGraphicWrap assetWrap) {
  switch (assetWrap) {
  case AssetGraphicWrap_Repeat:
    return RvkSamplerWrap_Repeat;
  case AssetGraphicWrap_Clamp:
    return RvkSamplerWrap_Clamp;
  }
  diag_crash_msg("Unknown graphic wrap mode");
}

static RvkSamplerFilter rvk_graphic_filter(const AssetGraphicFilter assetFilter) {
  switch (assetFilter) {
  case AssetGraphicFilter_Linear:
    return RvkSamplerFilter_Linear;
  case AssetGraphicFilter_Nearest:
    return RvkSamplerFilter_Nearest;
  }
  diag_crash_msg("Unknown graphic filter mode");
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
  diag_crash_msg("Unknown graphic aniso mode");
}

static void rvk_graphic_desc_merge(RvkDescMeta* meta, const RvkDescMeta* other) {
  for (usize i = 0; i != rvk_desc_bindings_max; ++i) {
    if (meta->bindings[i] && other->bindings[i]) {
      if (UNLIKELY(meta->bindings[i] == other->bindings[i])) {
        diag_crash_msg(
            "Incompatible shader descriptor binding {} ({} vs {})",
            fmt_int(i),
            fmt_text(rvk_desc_kind_str(meta->bindings[i])),
            fmt_text(rvk_desc_kind_str(other->bindings[i])));
      }
    } else if (other->bindings[i]) {
      meta->bindings[i] = other->bindings[i];
    }
  }
}

static RvkDescMeta rvk_graphic_desc_meta(const RvkGraphic* graphic, const usize set) {
  RvkDescMeta meta = {0};
  array_for_t(graphic->shaders, RvkShaderPtr, itr) {
    if (*itr) {
      rvk_graphic_desc_merge(&meta, &(*itr)->descriptors[set]);
    }
  }
  return meta;
}

static VkPipelineLayout rvk_pipeline_layout_create(const RvkGraphic* graphic) {
  const RvkDescMeta           globalDescMeta   = {.bindings[0] = RvkDescKind_UniformBufferDynamic};
  const RvkDescMeta           instanceDescMeta = {.bindings[0] = RvkDescKind_UniformBufferDynamic};
  const VkDescriptorSetLayout descriptorLayouts[] = {
      rvk_desc_vklayout(graphic->device->descPool, &globalDescMeta),
      rvk_desc_set_vklayout(graphic->descSet),
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
  if (!graphic->device->vkSupportedFeatures.fillModeNonSolid) {
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
  if (!dev->vkSupportedFeatures.wideLines) {
    return 1.0f;
  }
  return math_clamp_f32(
      graphic->lineWidth,
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
    return (VkPipelineColorBlendAttachmentState){
        .colorWriteMask = colorMask,
    };
  case AssetGraphicBlend_Count:
    break;
  }
  diag_crash();
}

static VkPipeline
rvk_pipeline_create(RvkGraphic* graphic, VkPipelineLayout layout, VkRenderPass vkRendPass) {

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
      .renderPass          = vkRendPass,
  };
  RvkDevice*      dev  = graphic->device;
  VkPipelineCache psoc = dev->vkPipelineCache;
  VkPipeline      result;
  rvk_call(vkCreateGraphicsPipelines, dev->vkDev, psoc, 1, &pipelineInfo, &dev->vkAlloc, &result);
  return result;
}

static void rvk_graphic_set_missing_sampler(RvkGraphic* graphic, const u32 samplerIndex) {
  diag_assert(!graphic->samplers[samplerIndex].texture);

  RvkDevice*  dev = graphic->device;
  RvkTexture* tex = rvk_repository_texture_get(dev->repository, RvkRepositoryId_MissingTexture);

  graphic->samplers[samplerIndex].texture = tex;
  graphic->samplers[samplerIndex].sampler = rvk_sampler_create(
      dev,
      RvkSamplerWrap_Repeat,
      RvkSamplerFilter_Nearest,
      RvkSamplerAniso_None,
      tex->image.mipLevels);
}

RvkGraphic*
rvk_graphic_create(RvkDevice* dev, const AssetGraphicComp* asset, const String dbgName) {
  RvkGraphic* graphic = alloc_alloc_t(g_alloc_heap, RvkGraphic);
  *graphic            = (RvkGraphic){
      .device     = dev,
      .dbgName    = string_dup(g_alloc_heap, dbgName),
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
      log_param("line-width", fmt_float(asset->lineWidth)),
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
  array_for_t(graphic->samplers, RvkGraphicSampler, itr) {
    if (itr->texture) {
      rvk_sampler_destroy(&itr->sampler, dev);
    }
  }
  string_free(g_alloc_heap, graphic->dbgName);
  alloc_free_t(g_alloc_heap, graphic);
}

void rvk_graphic_shader_add(RvkGraphic* graphic, RvkShader* shader) {
  array_for_t(graphic->shaders, RvkShaderPtr, itr) {
    if (!*itr) {
      *itr = shader;
      return;
    }
  }
  diag_assert_fail("More then {} shaders are not supported", fmt_int(rvk_graphic_shaders_max));
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
  diag_assert_fail("More then {} samplers are not supported", fmt_int(rvk_graphic_samplers_max));
}

u32 rvk_graphic_index_count(const RvkGraphic* graphic) {
  return graphic->mesh ? graphic->mesh->indexCount : 0;
}

bool rvk_graphic_prepare(RvkGraphic* graphic, VkCommandBuffer vkCmdBuf, VkRenderPass vkRendPass) {
  if (!graphic->vkPipeline) {
    const RvkDescMeta globalDescMeta = rvk_graphic_desc_meta(graphic, rvk_desc_global_set);
    if (globalDescMeta.bindings[0] == RvkDescKind_UniformBufferDynamic) {
      graphic->flags |= RvkGraphicFlags_GlobalData;
      // TODO: Verify that the other bindings in the global-set are unused.
    }
    const RvkDescMeta instanceDescMeta = rvk_graphic_desc_meta(graphic, rvk_desc_instance_set);
    if (instanceDescMeta.bindings[0] == RvkDescKind_UniformBufferDynamic) {
      graphic->flags |= RvkGraphicFlags_InstanceData;
      // TODO: Verify that the other bindings in the instance-set are unused.
    }

    const RvkDescMeta graphicDescMeta = rvk_graphic_desc_meta(graphic, rvk_desc_graphic_set);
    graphic->descSet                  = rvk_desc_alloc(graphic->device->descPool, &graphicDescMeta);

    // Attach mesh.
    if (graphicDescMeta.bindings[rvk_desc_graphic_bind_mesh] == RvkDescKind_StorageBuffer) {
      diag_assert_msg(graphic->mesh, "Shader expects a mesh but the graphic provides none");
      rvk_desc_set_attach_buffer(
          graphic->descSet, rvk_desc_graphic_bind_mesh, &graphic->mesh->vertexBuffer, 0);
    }

    // Attach samplers.
    u32 samplerIndex = 0;
    for (u32 i = 0; i != rvk_desc_bindings_max; ++i) {
      if (graphicDescMeta.bindings[i] == RvkDescKind_CombinedImageSampler) {
        if (UNLIKELY(i == rvk_graphic_samplers_max)) {
          diag_crash_msg("Shader expects more textures then is supported");
        }
        if (!graphic->samplers[samplerIndex].texture) {
          rvk_graphic_set_missing_sampler(graphic, samplerIndex);
        }
        const RvkImage*   image   = &graphic->samplers[samplerIndex].texture->image;
        const RvkSampler* sampler = &graphic->samplers[samplerIndex].sampler;
        rvk_desc_set_attach_sampler(graphic->descSet, i, image, sampler);
        ++samplerIndex;
      }
    }

    graphic->vkPipelineLayout = rvk_pipeline_layout_create(graphic);
    graphic->vkPipeline       = rvk_pipeline_create(graphic, graphic->vkPipelineLayout, vkRendPass);

    rvk_debug_name_pipeline_layout(
        graphic->device->debug, graphic->vkPipelineLayout, "{}", fmt_text(graphic->dbgName));
    rvk_debug_name_pipeline(
        graphic->device->debug, graphic->vkPipeline, "{}", fmt_text(graphic->dbgName));
  }
  if (!rvk_mesh_prepare(graphic->mesh)) {
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
      rvk_desc_graphic_set,
      1,
      &vkGraphicDescSet,
      0,
      null);

  if (graphic->mesh) {
    vkCmdBindIndexBuffer(vkCmdBuf, graphic->mesh->indexBuffer.vkBuffer, 0, VK_INDEX_TYPE_UINT16);
  }
}
