#include "asset/graphic.h"
#include "core/alloc.h"
#include "core/diag.h"
#include "core/math.h"
#include "core/stringtable.h"
#include "log/logger.h"
#include "rend/report.h"
#include "trace/tracer.h"

#include "desc.h"
#include "device.h"
#include "graphic.h"
#include "lib.h"
#include "mesh.h"
#include "pass.h"
#include "repository.h"
#include "sampler.h"
#include "shader.h"
#include "texture.h"

#define VOLO_RVK_GRAPHIC_VALIDATE_BIND 0
#define VOLO_RVK_GRAPHIC_REPORT_INTERNAL_DATA 1

static const u8 g_rendSupportedShaderSets[] = {
    RvkGraphicSet_Global,
    RvkGraphicSet_Draw,
    RvkGraphicSet_Graphic,
    RvkGraphicSet_Instance,
};

// clang-format off

#define rend_uniform_buffer_mask         (1 << RvkDescKind_UniformBuffer)
#define rend_storage_buffer_mask         (1 << RvkDescKind_StorageBuffer)
#define rend_image_sampler_2d_mask       (1 << RvkDescKind_CombinedImageSampler2D)
#define rend_image_sampler_2d_array_mask (1 << RvkDescKind_CombinedImageSampler2DArray)
#define rend_image_sampler_cube_mask     (1 << RvkDescKind_CombinedImageSamplerCube)
#define rend_image_sampler_mask          (rend_image_sampler_2d_mask | rend_image_sampler_2d_array_mask | rend_image_sampler_cube_mask)

// clang-format on

static const u32 g_rendSupportedGlobalBindings[rvk_desc_bindings_max] = {
    rend_uniform_buffer_mask,
    rend_image_sampler_2d_mask,
    rend_image_sampler_2d_mask,
    rend_image_sampler_2d_mask,
    rend_image_sampler_2d_mask,
    rend_image_sampler_2d_mask,
    rend_image_sampler_2d_mask,
    rend_image_sampler_2d_mask,
};

static const u32 g_rendSupportedDrawBindings[rvk_desc_bindings_max] = {
    rend_uniform_buffer_mask,
    rend_storage_buffer_mask,
    rend_image_sampler_mask,
};

static const u32 g_rendSupportedGraphicBindings[rvk_desc_bindings_max] = {
    rend_storage_buffer_mask,
    rend_image_sampler_mask,
    rend_image_sampler_mask,
    rend_image_sampler_mask,
    rend_image_sampler_mask,
    rend_image_sampler_mask,
    rend_image_sampler_mask,
    rend_image_sampler_mask,
};

static const u32 g_rendSupportedInstanceBindings[rvk_desc_bindings_max] = {
    rend_uniform_buffer_mask,
};

static bool rvk_desc_is_sampler(const RvkDescKind kind) {
  switch (kind) {
  case RvkDescKind_CombinedImageSampler2D:
  case RvkDescKind_CombinedImageSampler2DArray:
  case RvkDescKind_CombinedImageSamplerCube:
  case RvkDescKind_CombinedImageSamplerCubeArray:
    return true;
  default:
    return false;
  }
}

static const char* rvk_to_null_term_scratch(const String str) {
  const Mem scratchMem = alloc_alloc(g_allocScratch, str.size + 1, 1);
  mem_cpy(scratchMem, str);
  *mem_at_u8(scratchMem, str.size) = '\0';
  return scratchMem.ptr;
}

static RvkSamplerWrap rvk_graphic_wrap(const AssetGraphicWrap assetWrap) {
  switch (assetWrap) {
  case AssetGraphicWrap_Clamp:
    return RvkSamplerWrap_Clamp;
  case AssetGraphicWrap_Repeat:
    return RvkSamplerWrap_Repeat;
  case AssetGraphicWrap_Zero:
    return RvkSamplerWrap_Zero;
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
  for (u32 shaderIdx = 0; shaderIdx != array_elems(graphic->shaders); ++shaderIdx) {
    const RvkShader* shader = graphic->shaders[shaderIdx];
    if (!shader) {
      break;
    }
    if (UNLIKELY(!rvk_graphic_desc_merge(&meta, &shader->descriptors[set]))) {
      graphic->flags |= RvkGraphicFlags_Invalid;
    }
  }
  return meta;
}

static AssetGraphicBlend rvk_graphic_blend(const AssetGraphicComp* asset, const u32 outputBinding) {
  switch (outputBinding) {
  case 0:
    return asset->blend;
  default:
    return asset->blendAux;
  }
}

static bool rvk_graphic_blend_requires_alpha(const AssetGraphicBlend blend) {
  switch (blend) {
  case AssetGraphicBlend_Alpha:
  case AssetGraphicBlend_AlphaConstant:
  case AssetGraphicBlend_PreMultiplied:
    return true;
  case AssetGraphicBlend_Additive:
  case AssetGraphicBlend_None:
    return false;
  case AssetGraphicBlend_Count:
    break;
  }
  diag_crash();
}

static VkPipelineLayout
rvk_pipeline_layout_create(const RvkGraphic* graphic, RvkDevice* dev, const RvkPass* pass) {
  const RvkDescMeta           globalDescMeta      = rvk_pass_meta_global(pass);
  const RvkDescMeta           instanceDescMeta    = rvk_pass_meta_instance(pass);
  const VkDescriptorSetLayout descriptorLayouts[] = {
      rvk_desc_vklayout(dev->descPool, &globalDescMeta),
      rvk_desc_vklayout(dev->descPool, &graphic->drawDescMeta),
      rvk_desc_set_vklayout(graphic->graphicDescSet),
      rvk_desc_vklayout(dev->descPool, &instanceDescMeta),
  };
  const VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
      .sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = array_elems(descriptorLayouts),
      .pSetLayouts    = descriptorLayouts,
  };
  VkPipelineLayout result;
  rvk_call_checked(
      dev, createPipelineLayout, dev->vkDev, &pipelineLayoutInfo, &dev->vkAlloc, &result);
  return result;
}

static VkPipelineShaderStageCreateInfo rvk_pipeline_shader(
    const RvkShader* shader, const AssetGraphicOverride* overrides, const usize overrideCount) {
  VkSpecializationInfo* specialization = alloc_alloc_t(g_allocScratch, VkSpecializationInfo);

  *specialization = rvk_shader_specialize_scratch(shader, overrides, overrideCount);

  return (VkPipelineShaderStageCreateInfo){
      .sType               = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage               = shader->vkStage,
      .module              = shader->vkModule,
      .pName               = rvk_to_null_term_scratch(shader->entryPoint),
      .pSpecializationInfo = specialization,
  };
}

static VkPrimitiveTopology rvk_pipeline_input_topology(const AssetGraphicComp* asset) {
  switch (asset->topology) {
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

static VkPolygonMode rvk_pipeline_polygonmode(const AssetGraphicComp* asset, const RvkDevice* dev) {
  if (!(dev->flags & RvkDeviceFlags_SupportFillNonSolid)) {
    return VK_POLYGON_MODE_FILL;
  }
  switch (asset->rasterizer) {
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

static f32 rvk_pipeline_linewidth(const AssetGraphicComp* asset, const RvkDevice* dev) {
  if (!(dev->flags & RvkDeviceFlags_SupportWideLines)) {
    return 1.0f;
  }
  return math_clamp_f32(
      asset->lineWidth ? asset->lineWidth : 1.0f,
      dev->vkProperties.limits.lineWidthRange[0],
      dev->vkProperties.limits.lineWidthRange[1]);
}

static VkCullModeFlags rvk_pipeline_cullmode(const AssetGraphicComp* asset) {
  switch (asset->cull) {
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

static VkCompareOp rvk_pipeline_depth_compare(const AssetGraphicComp* asset) {
  switch (asset->depth) {
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
  case AssetGraphicDepth_Greater:
  case AssetGraphicDepth_GreaterNoWrite:
    // Use the 'less' compare op, because we are using a reversed-z depthbuffer.
    return VK_COMPARE_OP_LESS;
  case AssetGraphicDepth_GreaterOrEqual:
  case AssetGraphicDepth_GreaterOrEqualNoWrite:
    return VK_COMPARE_OP_LESS_OR_EQUAL;
  case AssetGraphicDepth_Always:
  case AssetGraphicDepth_AlwaysNoWrite:
    return VK_COMPARE_OP_ALWAYS;
  case AssetGraphicDepth_Count:
    break;
  }
  diag_crash();
}

static bool rvk_pipeline_depth_write(const AssetGraphicComp* asset) {
  switch (asset->depth) {
  case AssetGraphicDepth_Less:
  case AssetGraphicDepth_LessOrEqual:
  case AssetGraphicDepth_Equal:
  case AssetGraphicDepth_Greater:
  case AssetGraphicDepth_GreaterOrEqual:
  case AssetGraphicDepth_Always:
    return true;
  case AssetGraphicDepth_LessNoWrite:
  case AssetGraphicDepth_LessOrEqualNoWrite:
  case AssetGraphicDepth_EqualNoWrite:
  case AssetGraphicDepth_GreaterNoWrite:
  case AssetGraphicDepth_GreaterOrEqualNoWrite:
  case AssetGraphicDepth_AlwaysNoWrite:
    return false;
  case AssetGraphicDepth_Count:
    break;
  }
  diag_crash();
}

static bool rvk_pipeline_depth_test(const AssetGraphicComp* asset) {
  switch (asset->depth) {
  case AssetGraphicDepth_Always:
  case AssetGraphicDepth_AlwaysNoWrite:
    return false;
  default:
    return true;
  }
}

static bool rvk_pipeline_depth_clamp(const AssetGraphicComp* asset, const RvkDevice* dev) {
  if (!(dev->flags & RvkDeviceFlags_SupportDepthClamp)) {
    log_w("Device does not support depth-clamping");
    return false;
  }
  return asset->depthClamp;
}

static VkPipelineColorBlendAttachmentState rvk_pipeline_colorblend(const AssetGraphicBlend blend) {
  const VkColorComponentFlags colorMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
  switch (blend) {
  case AssetGraphicBlend_Alpha:
    return (VkPipelineColorBlendAttachmentState){
        .blendEnable         = true,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp        = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
        .alphaBlendOp        = VK_BLEND_OP_ADD,
        .colorWriteMask      = colorMask,
    };
  case AssetGraphicBlend_AlphaConstant:
    return (VkPipelineColorBlendAttachmentState){
        .blendEnable         = true,
        .srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA,
        .dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        .colorBlendOp        = VK_BLEND_OP_ADD,
        .srcAlphaBlendFactor = VK_BLEND_FACTOR_CONSTANT_ALPHA,
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
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
        .dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE,
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

static String rvk_pipeline_exec_name(const VkPipelineExecutablePropertiesKHR* props) {
  if (props->stages & VK_SHADER_STAGE_VERTEX_BIT) {
    return string_lit("Exec Vertex");
  }
  if (props->stages & VK_SHADER_STAGE_FRAGMENT_BIT) {
    return string_lit("Exec Fragment");
  }
  return string_from_null_term(props->name);
}

static const RvkShader*
rvk_pipeline_exec_shader(RvkGraphic* graphic, const VkPipelineExecutablePropertiesKHR* props) {
  for (u32 shaderIdx = 0; shaderIdx != array_elems(graphic->shaders); ++shaderIdx) {
    const RvkShader* shader = graphic->shaders[shaderIdx];
    if (!shader) {
      break;
    }
    if (props->stages & shader->vkStage) {
      return shader;
    }
  }
  return null;
}

static void rvk_pipeline_report_stats(
    RvkDevice* dev, RvkGraphic* graphic, VkPipeline vkPipeline, RendReport* report) {

  const VkPipelineInfoKHR pipelineInfo = {
      .sType    = VK_STRUCTURE_TYPE_PIPELINE_INFO_KHR,
      .pipeline = vkPipeline,
  };

  VkPipelineExecutablePropertiesKHR execProps[4];
  u32                               execCount = array_elems(execProps);

  for (u32 i = 0; i != array_elems(execProps); ++i) {
    execProps[i] = (VkPipelineExecutablePropertiesKHR){
        .sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_PROPERTIES_KHR,
    };
  }

  rvk_call_checked(
      dev, getPipelineExecutablePropertiesKHR, dev->vkDev, &pipelineInfo, &execCount, execProps);

  for (u32 execIndex = 0; execIndex != execCount; ++execIndex) {
    rend_report_push_section(report, rvk_pipeline_exec_name(&execProps[execIndex]));

    const RvkShader* shader = rvk_pipeline_exec_shader(graphic, &execProps[execIndex]);
    if (shader) {
      rend_report_push_value(report, string_lit("Shader"), string_empty, shader->dbgName);
      rend_report_push_value(
          report, string_lit("Shader entry"), string_lit("Shader entry point"), shader->entryPoint);

      rend_report_push_value(
          report,
          string_lit("Shader inputs"),
          string_empty,
          asset_shader_type_array_name_scratch(shader->inputs, asset_shader_max_inputs));

      rend_report_push_value(
          report,
          string_lit("Shader outputs"),
          string_empty,
          asset_shader_type_array_name_scratch(shader->outputs, asset_shader_max_outputs));
    }

    if (execProps[execIndex].subgroupSize) {
      rend_report_push_value(
          report,
          string_lit("Subgroup Size"),
          string_lit("Pipeline executable dispatch subgroup size"),
          fmt_write_scratch("{}", fmt_int(execProps[execIndex].subgroupSize)));
    }

    const VkPipelineExecutableInfoKHR execInfo = {
        .sType           = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INFO_KHR,
        .pipeline        = vkPipeline,
        .executableIndex = execIndex,
    };

    VkPipelineExecutableStatisticKHR stats[32];
    u32                              statCount = array_elems(stats);

    for (u32 i = 0; i != array_elems(stats); ++i) {
      stats[i] = (VkPipelineExecutableStatisticKHR){
          .sType = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_STATISTIC_KHR,
      };
    }

    rvk_call_checked(
        dev, getPipelineExecutableStatisticsKHR, dev->vkDev, &execInfo, &statCount, stats);

    for (u32 statIndex = 0; statIndex != statCount; ++statIndex) {
      const String statName = string_from_null_term(stats[statIndex].name);
      const String statDesc = string_from_null_term(stats[statIndex].description);

      String statValue;
      switch (stats[statIndex].format) {
      case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_BOOL32_KHR:
        statValue = fmt_write_scratch("{}", fmt_bool((bool)stats[statIndex].value.b32));
        break;
      case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_INT64_KHR:
        statValue = fmt_write_scratch("{}", fmt_int(stats[statIndex].value.i64));
        break;
      case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_UINT64_KHR:
        statValue = fmt_write_scratch("{}", fmt_int(stats[statIndex].value.u64));
        break;
      case VK_PIPELINE_EXECUTABLE_STATISTIC_FORMAT_FLOAT64_KHR:
        statValue = fmt_write_scratch("{}", fmt_float(stats[statIndex].value.f64));
        break;
      default:
        UNREACHABLE
      }
      rend_report_push_value(report, statName, statDesc, statValue);
    }

#if VOLO_RVK_GRAPHIC_REPORT_INTERNAL_DATA
    VkPipelineExecutableInternalRepresentationKHR data[4];
    u32                                           dataCount   = array_elems(data);
    const usize                                   dataMaxSize = 64 * usize_kibibyte;

    for (u32 i = 0; i != array_elems(data); ++i) {
      data[i] = (VkPipelineExecutableInternalRepresentationKHR){
          .sType    = VK_STRUCTURE_TYPE_PIPELINE_EXECUTABLE_INTERNAL_REPRESENTATION_KHR,
          .dataSize = dataMaxSize,
          .pData    = alloc_alloc(g_allocScratch, dataMaxSize, alignof(u8)).ptr,
      };
    }

    rvk_call_checked(
        dev,
        getPipelineExecutableInternalRepresentationsKHR,
        dev->vkDev,
        &execInfo,
        &dataCount,
        data);

    for (u32 dataIndex = 0; dataIndex != dataCount; ++dataIndex) {
      if (data[dataIndex].isText) {
        const bool pushed = rend_report_push_value(
            report,
            string_from_null_term(data[dataIndex].name),
            string_from_null_term(data[dataIndex].description),
            string_clamp(string_from_null_term(data[dataIndex].pData), dataMaxSize));

        if (!pushed) {
          log_w("Failed to report graphic data value");
        }
      }
    }
#endif
  }

  // Clear the section.
  rend_report_push_section(report, string_empty);
}

static VkPipeline rvk_pipeline_create(
    RvkGraphic*             graphic,
    const AssetGraphicComp* asset,
    RvkDevice*              dev,
    const VkPipelineLayout  layout,
    const RvkPass*          pass,
    RendReport*             report) {
  const RvkPassConfig* passConfig = rvk_pass_config(pass);

  VkPipelineShaderStageCreateInfo shaderStages[rvk_graphic_shaders_max];
  u32                             shaderStageCount = 0;
  for (u32 shaderIdx = 0; shaderIdx != array_elems(graphic->shaders); ++shaderIdx) {
    const RvkShader* shader = graphic->shaders[shaderIdx];
    if (!shader) {
      break;
    }
    const AssetGraphicOverride* overrides     = asset->shaders.values[shaderIdx].overrides.values;
    const usize                 overrideCount = asset->shaders.values[shaderIdx].overrides.count;

    if (rvk_shader_may_kill(shader, overrides, overrideCount)) {
      graphic->flags |= RvkGraphicFlags_MayDiscard;
    }

    shaderStages[shaderStageCount++] = rvk_pipeline_shader(shader, overrides, overrideCount);
  }

  const VkPipelineVertexInputStateCreateInfo vertexInputInfo = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
  };
  const VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
      .sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
      .topology = rvk_pipeline_input_topology(asset),
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

  const bool depthBiasEnabled =
      math_abs(asset->depthBiasConstant) > 1e-4f || math_abs(asset->depthBiasSlope) > 1e-4f;
  const VkPipelineRasterizationStateCreateInfo rasterizer = {
      .sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
      .polygonMode             = rvk_pipeline_polygonmode(asset, dev),
      .lineWidth               = rvk_pipeline_linewidth(asset, dev),
      .cullMode                = rvk_pipeline_cullmode(asset),
      .frontFace               = VK_FRONT_FACE_COUNTER_CLOCKWISE,
      .depthClampEnable        = rvk_pipeline_depth_clamp(asset, dev),
      .depthBiasEnable         = depthBiasEnabled,
      .depthBiasConstantFactor = depthBiasEnabled ? asset->depthBiasConstant : 0,
      .depthBiasSlopeFactor    = depthBiasEnabled ? asset->depthBiasSlope : 0,
  };

  const VkPipelineMultisampleStateCreateInfo multisampling = {
      .sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
      .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
  };

  const bool passHasDepth = passConfig->attachDepth != RvkPassDepth_None;
  const VkPipelineDepthStencilStateCreateInfo depthStencil = {
      .sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthWriteEnable = passHasDepth && rvk_pipeline_depth_write(asset),
      .depthTestEnable  = passHasDepth && rvk_pipeline_depth_test(asset),
      .depthCompareOp   = rvk_pipeline_depth_compare(asset),
  };

  u32                                 colorAttachmentCount = 0;
  VkPipelineColorBlendAttachmentState colorBlends[rvk_pass_attach_color_max];
  for (u32 binding = 0; binding != rvk_pass_attach_color_max; ++binding) {
    if (passConfig->attachColorFormat[binding]) {
      const AssetGraphicBlend blend       = rvk_graphic_blend(asset, binding);
      colorBlends[colorAttachmentCount++] = rvk_pipeline_colorblend(blend);
    }
  }
  const VkPipelineColorBlendStateCreateInfo colorBlending = {
      .sType             = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount   = colorAttachmentCount,
      .pAttachments      = colorBlends,
      .blendConstants[0] = asset->blendConstant,
      .blendConstants[1] = asset->blendConstant,
      .blendConstants[2] = asset->blendConstant,
      .blendConstants[3] = asset->blendConstant,
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

  VkPipelineCreateFlagBits createFlags = 0;
  if (report && dev->flags & RvkDeviceFlags_SupportExecutableInfo) {
    createFlags |= VK_PIPELINE_CREATE_CAPTURE_STATISTICS_BIT_KHR;
#if VOLO_RVK_GRAPHIC_REPORT_INTERNAL_DATA
    createFlags |= VK_PIPELINE_CREATE_CAPTURE_INTERNAL_REPRESENTATIONS_BIT_KHR;
#endif
  }

  const VkGraphicsPipelineCreateInfo info = {
      .sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .flags               = createFlags,
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

  VkPipeline result;
  trace_begin("rend_pipeline_create", TraceColor_Blue);
  {
    VkPipelineCache cache = dev->vkPipelineCache;
    rvk_call_checked(
        dev, createGraphicsPipelines, dev->vkDev, cache, 1, &info, &dev->vkAlloc, &result);
  }
  trace_end();

  if (report && dev->flags & RvkDeviceFlags_SupportExecutableInfo) {
    rvk_pipeline_report_stats(dev, graphic, result, report);
  }

  return result;
}

static void rvk_graphic_set_missing_sampler(
    RvkGraphic*          graphic,
    const RvkRepository* repo,
    const u32            samplerIndex,
    const RvkDescKind    kind) {
  diag_assert(!graphic->samplerTextures[samplerIndex]);

  RvkRepositoryId repoId;
  switch (kind) {
  case RvkDescKind_CombinedImageSampler2DArray:
    repoId = RvkRepositoryId_MissingTextureArray;
    break;
  case RvkDescKind_CombinedImageSamplerCube:
    repoId = RvkRepositoryId_MissingTextureCube;
    break;
  default:
    repoId = RvkRepositoryId_MissingTexture;
    break;
  }

  graphic->samplerTextures[samplerIndex] = rvk_repository_texture_get(repo, repoId);
  graphic->samplerSpecs[samplerIndex]    = (RvkSamplerSpec){
         .wrap   = RvkSamplerWrap_Repeat,
         .filter = RvkSamplerFilter_Nearest,
  };
}

static AssetShaderType rvk_graphic_pass_shader_output(const RvkPassFormat passFormat) {
  switch (passFormat) {
  case RvkPassFormat_None:
    return AssetShaderType_None;
  case RvkPassFormat_Color1Linear:
    return AssetShaderType_f32;
  case RvkPassFormat_Color2Linear:
  case RvkPassFormat_Color2SignedFloat:
    return AssetShaderType_f32v2;
  case RvkPassFormat_Color3LowPrecision:
  case RvkPassFormat_Color3Float:
    return AssetShaderType_f32v3;
  case RvkPassFormat_Color4Linear:
  case RvkPassFormat_Color4Srgb:
  case RvkPassFormat_Swapchain:
    return AssetShaderType_f32v4;
  }
  diag_crash();
}

static bool rvk_graphic_validate_shaders(
    const RvkGraphic* graphic, const AssetGraphicComp* asset, const RvkPass* pass) {

  const RvkShader*      shaderVert  = null;
  const RvkShader*      shaderFrag  = null;
  VkShaderStageFlagBits foundStages = 0;

  for (u32 shaderIdx = 0; shaderIdx != array_elems(graphic->shaders); ++shaderIdx) {
    const RvkShader* shader = graphic->shaders[shaderIdx];
    if (!shader) {
      break;
    }

    // Validate stage.
    if (UNLIKELY(foundStages & shader->vkStage)) {
      log_e("Duplicate shader stage", log_param("graphic", fmt_text(graphic->dbgName)));
      return false;
    }
    foundStages |= shader->vkStage;

    switch (shader->vkStage) {
    case VK_SHADER_STAGE_VERTEX_BIT:
      shaderVert = shader;
      break;
    case VK_SHADER_STAGE_FRAGMENT_BIT:
      shaderFrag = shader;
      break;
    default:
      UNREACHABLE
    }

    // Validate used sets.
    for (u32 set = 0; set != rvk_shader_desc_max; ++set) {
      const bool supported = mem_contains(mem_var(g_rendSupportedShaderSets), set);
      if (UNLIKELY(!supported && rvk_shader_set_used(shader, set))) {
        const StringHash          shaderIdHash = asset->shaders.values[shaderIdx].program.id;
        MAYBE_UNUSED const String shaderId     = stringtable_lookup(g_stringtable, shaderIdHash);

        log_e(
            "Shader uses unsupported set",
            log_param("graphic", fmt_text(graphic->dbgName)),
            log_param("shader", fmt_text(shaderId)),
            log_param("set", fmt_int(set)));
        return false;
      }
    }
  }

  if (UNLIKELY(!shaderVert)) {
    log_e("Vertex shader missing", log_param("graphic", fmt_text(graphic->dbgName)));
    return false;
  }
  if (UNLIKELY(!shaderFrag)) {
    log_e("Vertex shader missing", log_param("graphic", fmt_text(graphic->dbgName)));
    return false;
  }

  // Validate fragment inputs.
  ASSERT(asset_shader_max_outputs >= asset_shader_max_inputs, "Not enough shader outputs");
  for (u32 binding = 0; binding != asset_shader_max_inputs; ++binding) {
    const AssetShaderType inputType  = shaderFrag->inputs[binding];
    const AssetShaderType outputType = shaderVert->outputs[binding];
    if (inputType == AssetShaderType_None) {
      continue; // Binding unused.
    }
    if (UNLIKELY(outputType != inputType)) {
      log_e(
          "Unsatisfied fragment shader input binding",
          log_param("graphic", fmt_text(graphic->dbgName)),
          log_param("binding", fmt_int(binding)),
          log_param("fragment-input", fmt_text(asset_shader_type_name(inputType))),
          log_param("vertex-output", fmt_text(asset_shader_type_name(outputType))));
      return false;
    }
  }

  // Validate fragment outputs.
  const RvkPassConfig* passConfig = rvk_pass_config(pass);
  for (u32 binding = 0; binding != asset_shader_max_outputs; ++binding) {
    const AssetShaderType   outputType  = shaderFrag->outputs[binding];
    const AssetGraphicBlend outputBlend = rvk_graphic_blend(asset, binding);
    if (binding >= rvk_pass_attach_color_max) {
      if (UNLIKELY(outputType != AssetShaderType_None)) {
        log_e(
            "Fragment shader output binding not consumed by pass",
            log_param("graphic", fmt_text(graphic->dbgName)),
            log_param("pass", fmt_text(passConfig->name)),
            log_param("binding", fmt_int(binding)),
            log_param("type", fmt_text(asset_shader_type_name(outputType))));
        return false;
      }
      continue; // Output binding not used by pass.
    }
    AssetShaderType passOutputType;
    if (passConfig->attachColorFormat[binding] && rvk_graphic_blend_requires_alpha(outputBlend)) {
      passOutputType = AssetShaderType_f32v4;
    } else {
      passOutputType = rvk_graphic_pass_shader_output(passConfig->attachColorFormat[binding]);
    }
    if (UNLIKELY(outputType != passOutputType)) {
      log_e(
          "Fragment shader output binding invalid",
          log_param("graphic", fmt_text(graphic->dbgName)),
          log_param("pass", fmt_text(passConfig->name)),
          log_param("binding", fmt_int(binding)),
          log_param("expected-type", fmt_text(asset_shader_type_name(passOutputType))),
          log_param("actual-type", fmt_text(asset_shader_type_name(outputType))));
      return false;
    }
  }

  return true;
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
  (void)dev;
  RvkGraphic* graphic = alloc_alloc_t(g_allocHeap, RvkGraphic);

  *graphic = (RvkGraphic){
      .dbgName     = string_dup(g_allocHeap, dbgName),
      .passId      = asset->pass,
      .passOrder   = asset->passOrder,
      .passReq     = asset->passRequirements,
      .vertexCount = asset->vertexCount,
  };

  log_d("Vulkan graphic created", log_param("name", fmt_text(dbgName)));

  return graphic;
}

void rvk_graphic_destroy(RvkGraphic* graphic, RvkDevice* dev) {
  if (graphic->vkPipeline) {
    rvk_call(dev, destroyPipeline, dev->vkDev, graphic->vkPipeline, &dev->vkAlloc);
  }
  if (graphic->vkPipelineLayout) {
    rvk_call(dev, destroyPipelineLayout, dev->vkDev, graphic->vkPipelineLayout, &dev->vkAlloc);
  }
  if (rvk_desc_valid(graphic->graphicDescSet)) {
    rvk_desc_free(graphic->graphicDescSet);
  }

  log_d("Vulkan graphic destroyed", log_param("name", fmt_text(graphic->dbgName)));

  string_free(g_allocHeap, graphic->dbgName);
  alloc_free_t(g_allocHeap, graphic);
}

void rvk_graphic_add_shader(RvkGraphic* graphic, const RvkShader* shader) {
  for (u32 shaderIdx = 0; shaderIdx != array_elems(graphic->shaders); ++shaderIdx) {
    if (!graphic->shaders[shaderIdx]) {
      graphic->shaders[shaderIdx] = shader;
      return;
    }
  }

  log_e(
      "Shaders limit exceeded",
      log_param("graphic", fmt_text(graphic->dbgName)),
      log_param("limit", fmt_int(rvk_graphic_shaders_max)));
}

void rvk_graphic_add_mesh(RvkGraphic* graphic, const RvkMesh* mesh) {
  diag_assert_msg(!graphic->mesh, "Only a single mesh per graphic supported");
  graphic->mesh = mesh;
}

void rvk_graphic_add_sampler(
    RvkGraphic*                graphic,
    const AssetGraphicComp*    asset,
    const RvkTexture*          tex,
    const AssetGraphicSampler* sampler) {
  (void)asset;

  for (u8 samplerIndex = 0; samplerIndex != rvk_graphic_samplers_max; ++samplerIndex) {
    if (!graphic->samplerTextures[samplerIndex]) {
      RvkSamplerFlags samplerFlags = RvkSamplerFlags_None;
      if (sampler->mipBlending) {
        samplerFlags |= RvkSamplerFlags_MipBlending;
      }
      graphic->samplerMask |= 1 << samplerIndex;
      graphic->samplerTextures[samplerIndex] = tex;
      graphic->samplerSpecs[samplerIndex]    = (RvkSamplerSpec){
             .flags  = samplerFlags,
             .wrap   = rvk_graphic_wrap(sampler->wrap),
             .filter = rvk_graphic_filter(sampler->filter),
             .aniso  = rvk_graphic_aniso(sampler->anisotropy),
      };
      return;
    }
  }
  log_e(
      "Sampler limit exceeded",
      log_param("graphic", fmt_text(graphic->dbgName)),
      log_param("limit", fmt_int(rvk_graphic_samplers_max)));
}

bool rvk_graphic_finalize(
    RvkGraphic*             gra,
    const AssetGraphicComp* asset,
    RvkDevice*              dev,
    const RvkPass*          pass,
    RendReport*             report) {
  diag_assert_msg(!gra->vkPipeline, "Graphic already finalized");
  diag_assert((u32)gra->passId == rvk_pass_config(pass)->id);

  const TimeSteady startTime = time_steady_clock();

  RvkDescUpdateBatch descBatch;
  descBatch.count = 0;

  if (UNLIKELY(!rvk_graphic_validate_shaders(gra, asset, pass))) {
    gra->flags |= RvkGraphicFlags_Invalid;
  }

  // Finalize global set bindings.
  const RvkDescMeta globalDescMeta = rvk_graphic_desc_meta(gra, RvkGraphicSet_Global);
  if (UNLIKELY(!rend_graphic_validate_set(
          gra, RvkGraphicSet_Global, &globalDescMeta, g_rendSupportedGlobalBindings))) {
    gra->flags |= RvkGraphicFlags_Invalid;
  }
  for (u16 i = 0; i != rvk_desc_bindings_max; ++i) {
    if (globalDescMeta.bindings[i]) {
      gra->globalBindings |= 1 << i;
    }
  }

  // Finalize draw bindings.
  const RvkDescMeta drawDescMeta = rvk_graphic_desc_meta(gra, RvkGraphicSet_Draw);
  if (UNLIKELY(!rend_graphic_validate_set(
          gra, RvkGraphicSet_Draw, &drawDescMeta, g_rendSupportedDrawBindings))) {
    gra->flags |= RvkGraphicFlags_Invalid;
  }
  if (!rvk_desc_empty(&drawDescMeta)) {
    gra->flags |= RvkGraphicFlags_RequireDrawSet;
  }
  gra->drawDescMeta = drawDescMeta;

  // Finalize graphic set bindings.
  const RvkDescMeta graphicDescMeta = rvk_graphic_desc_meta(gra, RvkGraphicSet_Graphic);
  if (UNLIKELY(!rend_graphic_validate_set(
          gra, RvkGraphicSet_Graphic, &graphicDescMeta, g_rendSupportedGraphicBindings))) {
    gra->flags |= RvkGraphicFlags_Invalid;
  }
  gra->graphicDescSet = rvk_desc_alloc(dev->descPool, &graphicDescMeta);
  rvk_desc_set_name(gra->graphicDescSet, gra->dbgName);

  // Finalize instance set bindings.
  const RvkDescMeta instanceDescMeta = rvk_graphic_desc_meta(gra, RvkGraphicSet_Instance);
  if (UNLIKELY(!rend_graphic_validate_set(
          gra, RvkGraphicSet_Instance, &instanceDescMeta, g_rendSupportedInstanceBindings))) {
    gra->flags |= RvkGraphicFlags_Invalid;
  }
  if (!rvk_desc_empty(&instanceDescMeta)) {
    gra->flags |= RvkGraphicFlags_RequireInstanceSet;
  }

  // Attach mesh.
  if (graphicDescMeta.bindings[0] == RvkDescKind_StorageBuffer) {
    if (UNLIKELY(!gra->mesh)) {
      gra->mesh = rvk_repository_mesh_get(dev->repository, RvkRepositoryId_MissingMesh);
      /**
       * NOTE: Treat a missing mesh as an error (as opposed to a missing texture), reason is for
       * meshes (especially skinned meshes) the scale of a replacement mesh might be way off.
       */
      log_e("Shader requires a mesh", log_param("graphic", fmt_text(gra->dbgName)));
      gra->flags |= RvkGraphicFlags_Invalid;
    }
    rvk_desc_update_buffer(&descBatch, gra->graphicDescSet, 0, &gra->mesh->vertexBuffer, 0, 0);
  }
  if (UNLIKELY(gra->mesh && gra->drawDescMeta.bindings[1])) {
    log_e(
        "Graphic cannot use both a normal and a per-draw mesh ",
        log_param("graphic", fmt_text(gra->dbgName)));
    gra->flags |= RvkGraphicFlags_Invalid;
  }

  // Attach samplers.
  u32 samplerIndex = 0;
  for (u32 i = 0; i != rvk_desc_bindings_max; ++i) {
    const RvkDescKind kind = graphicDescMeta.bindings[i];
    if (rvk_desc_is_sampler(kind)) {
      if (UNLIKELY(samplerIndex == rvk_graphic_samplers_max)) {
        log_e(
            "Shader exceeds texture limit",
            log_param("graphic", fmt_text(gra->dbgName)),
            log_param("limit", fmt_int(rvk_graphic_samplers_max)));
        gra->flags |= RvkGraphicFlags_Invalid;
        break;
      }
      if (!gra->samplerTextures[samplerIndex]) {
        rvk_graphic_set_missing_sampler(gra, dev->repository, samplerIndex, kind);
      }
      if (kind != rvk_texture_sampler_kind(gra->samplerTextures[samplerIndex])) {
        log_e(
            "Mismatched shader texture sampler kind",
            log_param("graphic", fmt_text(gra->dbgName)),
            log_param("sampler", fmt_int(samplerIndex)),
            log_param("expected", fmt_text(rvk_desc_kind_str(kind))));
        gra->flags |= RvkGraphicFlags_Invalid;
        break;
      }
      const RvkImage*      image       = &gra->samplerTextures[samplerIndex]->image;
      const RvkSamplerSpec samplerSpec = gra->samplerSpecs[samplerIndex];
      rvk_desc_update_sampler(&descBatch, gra->graphicDescSet, i, image, samplerSpec);
      ++samplerIndex;
    }
  }

  if (report) {
    rend_report_push_value(
        report,
        string_lit("Pass"),
        string_empty,
        asset_graphic_pass_name((AssetGraphicPass)gra->passId));
    rend_report_push_value(
        report,
        string_lit("Pass order"),
        string_empty,
        fmt_write_scratch("{}", fmt_int(gra->passOrder)));
  }

  if (gra->flags & RvkGraphicFlags_Invalid) {
    return false;
  }

  rvk_desc_update_flush(&descBatch);

  gra->vkPipelineLayout = rvk_pipeline_layout_create(gra, dev, pass);
  gra->vkPipeline       = rvk_pipeline_create(gra, asset, dev, gra->vkPipelineLayout, pass, report);

  const TimeDuration dur = time_steady_duration(startTime, time_steady_clock());
  if (report) {
    rend_report_push_value(
        report,
        string_lit("Finalize duration"),
        string_empty,
        fmt_write_scratch("{}", fmt_duration(dur)));
  }

  rvk_debug_name_pipeline_layout(dev, gra->vkPipelineLayout, "{}", fmt_text(gra->dbgName));
  rvk_debug_name_pipeline(dev, gra->vkPipeline, "{}", fmt_text(gra->dbgName));
  return true;
}

bool rvk_graphic_is_ready(const RvkGraphic* graphic, const RvkDevice* dev) {
  if (UNLIKELY(graphic->flags & RvkGraphicFlags_Invalid)) {
    return false;
  }
  diag_assert_msg(graphic->vkPipeline, "Graphic not finalized");
  if (graphic->mesh && !rvk_mesh_is_ready(graphic->mesh, dev)) {
    return false;
  }
  for (u32 samplerIndex = 0; samplerIndex != rvk_graphic_samplers_max; ++samplerIndex) {
    const RvkTexture* tex = graphic->samplerTextures[samplerIndex];
    if (tex && !rvk_texture_is_ready(tex, dev)) {
      return false;
    }
  }
  return true;
}

void rvk_graphic_bind(
    const RvkGraphic* graphic,
    const RvkDevice*  dev,
    const RvkPass*    pass,
    RvkDescGroup*     descGroup,
    VkCommandBuffer   vkCmdBuf) {
  (void)pass;
#if VOLO_RVK_GRAPHIC_VALIDATE_BIND
  diag_assert_msg(rvk_graphic_is_ready(graphic, dev), "Graphic is not ready");
  diag_assert_msg(rvk_pass_active(pass), "Pass not active");
#endif
  diag_assert_msg((u32)graphic->passId == rvk_pass_config(pass)->id, "Unsupported pass");

  rvk_call(dev, cmdBindPipeline, vkCmdBuf, VK_PIPELINE_BIND_POINT_GRAPHICS, graphic->vkPipeline);

  rvk_desc_group_bind(descGroup, RvkGraphicSet_Graphic, graphic->graphicDescSet);

  if (graphic->mesh) {
    rvk_mesh_bind(graphic->mesh, dev, vkCmdBuf);
  }
}
