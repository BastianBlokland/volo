#include "core_alloc.h"
#include "core_diag.h"
#include "log_logger.h"

#include "vulkan_internal.h"

static const char* rvk_to_null_term_scratch(const String str) {
  const Mem scratchMem = alloc_alloc(g_allocScratch, str.size + 1, 1);
  mem_cpy(scratchMem, str);
  *mem_at_u8(scratchMem, str.size) = '\0';
  return scratchMem.ptr;
}

void* rvk_func_load_instance_internal(VkInstance inst, const String api) {
  const PFN_vkVoidFunction res = vkGetInstanceProcAddr(inst, rvk_to_null_term_scratch(api));
  if (UNLIKELY(!res)) {
    diag_crash_msg("Vulkan failed to load instance api: {}", fmt_text(api));
  }
  return (void*)res;
}

void* rvk_func_load_device_internal(VkDevice inst, const String api) {
  const PFN_vkVoidFunction res = vkGetDeviceProcAddr(inst, rvk_to_null_term_scratch(api));
  if (UNLIKELY(!res)) {
    diag_crash_msg("Vulkan failed to load device api: {}", fmt_text(api));
  }
  return (void*)res;
}

void rvk_check(const String api, const VkResult result) {
  if (LIKELY(result == VK_SUCCESS)) {
    return;
  }
  if (result == VK_INCOMPLETE) {
    log_w("Vulkan {}: Result incomplete", log_param("api", fmt_text(api)));
    return;
  }
  diag_crash_msg(
      "Vulkan {}: [{}] {}", fmt_text(api), fmt_int(result), fmt_text(rvk_result_str(result)));
}

String rvk_result_str(const VkResult result) {
#define RET_STR(NAME)                                                                              \
  case VK_##NAME:                                                                                  \
    return string_lit(#NAME)

  switch (result) {
    RET_STR(SUCCESS);
    RET_STR(NOT_READY);
    RET_STR(TIMEOUT);
    RET_STR(EVENT_SET);
    RET_STR(EVENT_RESET);
    RET_STR(INCOMPLETE);
    RET_STR(ERROR_OUT_OF_HOST_MEMORY);
    RET_STR(ERROR_OUT_OF_DEVICE_MEMORY);
    RET_STR(ERROR_INITIALIZATION_FAILED);
    RET_STR(ERROR_DEVICE_LOST);
    RET_STR(ERROR_MEMORY_MAP_FAILED);
    RET_STR(ERROR_LAYER_NOT_PRESENT);
    RET_STR(ERROR_EXTENSION_NOT_PRESENT);
    RET_STR(ERROR_FEATURE_NOT_PRESENT);
    RET_STR(ERROR_INCOMPATIBLE_DRIVER);
    RET_STR(ERROR_TOO_MANY_OBJECTS);
    RET_STR(ERROR_FORMAT_NOT_SUPPORTED);
    RET_STR(ERROR_FRAGMENTED_POOL);
    RET_STR(ERROR_UNKNOWN);
    RET_STR(ERROR_OUT_OF_POOL_MEMORY);
    RET_STR(ERROR_INVALID_EXTERNAL_HANDLE);
    RET_STR(ERROR_FRAGMENTATION);
    RET_STR(ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS);
    RET_STR(ERROR_SURFACE_LOST_KHR);
    RET_STR(ERROR_NATIVE_WINDOW_IN_USE_KHR);
    RET_STR(SUBOPTIMAL_KHR);
    RET_STR(ERROR_OUT_OF_DATE_KHR);
    RET_STR(ERROR_INCOMPATIBLE_DISPLAY_KHR);
    RET_STR(ERROR_VALIDATION_FAILED_EXT);
    RET_STR(ERROR_INVALID_SHADER_NV);
    RET_STR(ERROR_INVALID_DRM_FORMAT_MODIFIER_PLANE_LAYOUT_EXT);
    RET_STR(ERROR_NOT_PERMITTED_EXT);
    RET_STR(ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT);
    RET_STR(THREAD_IDLE_KHR);
    RET_STR(THREAD_DONE_KHR);
    RET_STR(OPERATION_DEFERRED_KHR);
    RET_STR(OPERATION_NOT_DEFERRED_KHR);
    RET_STR(PIPELINE_COMPILE_REQUIRED_EXT);
  default:
    return string_lit("UNKNOWN");
  }
#undef RET_STR
}

String rvk_devicetype_str(const VkPhysicalDeviceType type) {
  switch (type) {
  case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
    return string_lit("integrated");
  case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
    return string_lit("discrete");
  case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
    return string_lit("virtual");
  case VK_PHYSICAL_DEVICE_TYPE_CPU:
    return string_lit("cpu");
  default:
    return string_lit("other");
  }
}

String rvk_vendor_str(const u32 vendorId) {
  switch (vendorId) {
  case 0x1002:
    return string_lit("AMD");
  case 0x1010:
    return string_lit("ImgTec");
  case 0x10DE:
    return string_lit("NVIDIA");
  case 0x13B5:
    return string_lit("ARM");
  case 0x5143:
    return string_lit("Qualcomm");
  case 0x8086:
    return string_lit("INTEL");
  default:
    return string_lit("other");
  }
}

String rvk_colorspace_str(const VkColorSpaceKHR colorSpace) {
#define RET_STR(NAME)                                                                              \
  case VK_COLOR_SPACE_##NAME:                                                                      \
    return string_lit(#NAME)

  switch (colorSpace) {
    RET_STR(SRGB_NONLINEAR_KHR);
    RET_STR(DISPLAY_P3_NONLINEAR_EXT);
    RET_STR(EXTENDED_SRGB_LINEAR_EXT);
    RET_STR(DISPLAY_P3_LINEAR_EXT);
    RET_STR(DCI_P3_NONLINEAR_EXT);
    RET_STR(BT709_LINEAR_EXT);
    RET_STR(BT709_NONLINEAR_EXT);
    RET_STR(BT2020_LINEAR_EXT);
    RET_STR(HDR10_ST2084_EXT);
    RET_STR(DOLBYVISION_EXT);
    RET_STR(HDR10_HLG_EXT);
    RET_STR(ADOBERGB_LINEAR_EXT);
    RET_STR(ADOBERGB_NONLINEAR_EXT);
    RET_STR(PASS_THROUGH_EXT);
    RET_STR(EXTENDED_SRGB_NONLINEAR_EXT);
    RET_STR(DISPLAY_NATIVE_AMD);
  default:
    return string_lit("unknown");
  }
#undef RET_STR
}

String rvk_presentmode_str(const VkPresentModeKHR mode) {
#define RET_STR(NAME)                                                                              \
  case VK_PRESENT_MODE_##NAME:                                                                     \
    return string_lit(#NAME)

  switch (mode) {
    RET_STR(IMMEDIATE_KHR);
    RET_STR(MAILBOX_KHR);
    RET_STR(FIFO_KHR);
    RET_STR(FIFO_RELAXED_KHR);
    RET_STR(SHARED_DEMAND_REFRESH_KHR);
    RET_STR(SHARED_CONTINUOUS_REFRESH_KHR);
  default:
    return string_lit("unknown");
  }
#undef RET_STR
}

RvkFormatInfo rvk_format_info(const VkFormat format) {
#define RET_INFO(_NAME_, _SIZE_, _CHANNEL_COUNT_, _FLAGS_)                                         \
  case VK_FORMAT_##_NAME_:                                                                         \
    return (RvkFormatInfo) {                                                                       \
      .name = string_lit(#_NAME_), .size = _SIZE_, .channels = _CHANNEL_COUNT_, .flags = _FLAGS_,  \
    }

  switch (format) {
    RET_INFO(R4G4_UNORM_PACK8, 1, 2, 0);
    RET_INFO(R4G4B4A4_UNORM_PACK16, 2, 4, 0);
    RET_INFO(B4G4R4A4_UNORM_PACK16, 2, 4, 0);
    RET_INFO(R5G6B5_UNORM_PACK16, 2, 3, 0);
    RET_INFO(B5G6R5_UNORM_PACK16, 2, 3, 0);
    RET_INFO(R5G5B5A1_UNORM_PACK16, 2, 4, 0);
    RET_INFO(B5G5R5A1_UNORM_PACK16, 2, 4, 0);
    RET_INFO(A1R5G5B5_UNORM_PACK16, 2, 4, 0);
    RET_INFO(R8_UNORM, 1, 1, 0);
    RET_INFO(R8_SNORM, 1, 1, 0);
    RET_INFO(R8_USCALED, 1, 1, 0);
    RET_INFO(R8_SSCALED, 1, 1, 0);
    RET_INFO(R8_UINT, 1, 1, 0);
    RET_INFO(R8_SINT, 1, 1, 0);
    RET_INFO(R8_SRGB, 1, 1, RvkFormat_Srgb);
    RET_INFO(R8G8_UNORM, 2, 2, 0);
    RET_INFO(R8G8_SNORM, 2, 2, 0);
    RET_INFO(R8G8_USCALED, 2, 2, 0);
    RET_INFO(R8G8_SSCALED, 2, 2, 0);
    RET_INFO(R8G8_UINT, 2, 2, 0);
    RET_INFO(R8G8_SINT, 2, 2, 0);
    RET_INFO(R8G8_SRGB, 2, 2, RvkFormat_Srgb);
    RET_INFO(R8G8B8_UNORM, 3, 3, 0);
    RET_INFO(R8G8B8_SNORM, 3, 3, 0);
    RET_INFO(R8G8B8_USCALED, 3, 3, 0);
    RET_INFO(R8G8B8_SSCALED, 3, 3, 0);
    RET_INFO(R8G8B8_UINT, 3, 3, 0);
    RET_INFO(R8G8B8_SINT, 3, 3, 0);
    RET_INFO(R8G8B8_SRGB, 3, 3, RvkFormat_Srgb);
    RET_INFO(B8G8R8_UNORM, 3, 3, 0);
    RET_INFO(B8G8R8_SNORM, 3, 3, 0);
    RET_INFO(B8G8R8_USCALED, 3, 3, 0);
    RET_INFO(B8G8R8_SSCALED, 3, 3, 0);
    RET_INFO(B8G8R8_UINT, 3, 3, 0);
    RET_INFO(B8G8R8_SINT, 3, 3, 0);
    RET_INFO(B8G8R8_SRGB, 3, 3, RvkFormat_Srgb);
    RET_INFO(R8G8B8A8_UNORM, 4, 4, RvkFormat_RGBA);
    RET_INFO(R8G8B8A8_SNORM, 4, 4, RvkFormat_RGBA);
    RET_INFO(R8G8B8A8_USCALED, 4, 4, RvkFormat_RGBA);
    RET_INFO(R8G8B8A8_SSCALED, 4, 4, RvkFormat_RGBA);
    RET_INFO(R8G8B8A8_UINT, 4, 4, RvkFormat_RGBA);
    RET_INFO(R8G8B8A8_SINT, 4, 4, RvkFormat_RGBA);
    RET_INFO(R8G8B8A8_SRGB, 4, 4, RvkFormat_RGBA | RvkFormat_Srgb);
    RET_INFO(B8G8R8A8_UNORM, 4, 4, RvkFormat_BGRA);
    RET_INFO(B8G8R8A8_SNORM, 4, 4, RvkFormat_BGRA);
    RET_INFO(B8G8R8A8_USCALED, 4, 4, RvkFormat_BGRA);
    RET_INFO(B8G8R8A8_SSCALED, 4, 4, RvkFormat_BGRA);
    RET_INFO(B8G8R8A8_UINT, 4, 4, RvkFormat_BGRA);
    RET_INFO(B8G8R8A8_SINT, 4, 4, RvkFormat_BGRA);
    RET_INFO(B8G8R8A8_SRGB, 4, 4, RvkFormat_BGRA | RvkFormat_Srgb);
    RET_INFO(A8B8G8R8_UNORM_PACK32, 4, 4, 0);
    RET_INFO(A8B8G8R8_SNORM_PACK32, 4, 4, 0);
    RET_INFO(A8B8G8R8_USCALED_PACK32, 4, 4, 0);
    RET_INFO(A8B8G8R8_SSCALED_PACK32, 4, 4, 0);
    RET_INFO(A8B8G8R8_UINT_PACK32, 4, 4, 0);
    RET_INFO(A8B8G8R8_SINT_PACK32, 4, 4, 0);
    RET_INFO(A8B8G8R8_SRGB_PACK32, 4, 4, RvkFormat_Srgb);
    RET_INFO(A2R10G10B10_UNORM_PACK32, 4, 4, 0);
    RET_INFO(A2R10G10B10_SNORM_PACK32, 4, 4, 0);
    RET_INFO(A2R10G10B10_USCALED_PACK32, 4, 4, 0);
    RET_INFO(A2R10G10B10_SSCALED_PACK32, 4, 4, 0);
    RET_INFO(A2R10G10B10_UINT_PACK32, 4, 4, 0);
    RET_INFO(A2R10G10B10_SINT_PACK32, 4, 4, 0);
    RET_INFO(A2B10G10R10_UNORM_PACK32, 4, 4, 0);
    RET_INFO(A2B10G10R10_SNORM_PACK32, 4, 4, 0);
    RET_INFO(A2B10G10R10_USCALED_PACK32, 4, 4, 0);
    RET_INFO(A2B10G10R10_SSCALED_PACK32, 4, 4, 0);
    RET_INFO(A2B10G10R10_UINT_PACK32, 4, 4, 0);
    RET_INFO(A2B10G10R10_SINT_PACK32, 4, 4, 0);
    RET_INFO(R16_UNORM, 2, 1, 0);
    RET_INFO(R16_SNORM, 2, 1, 0);
    RET_INFO(R16_USCALED, 2, 1, 0);
    RET_INFO(R16_SSCALED, 2, 1, 0);
    RET_INFO(R16_UINT, 2, 1, 0);
    RET_INFO(R16_SINT, 2, 1, 0);
    RET_INFO(R16_SFLOAT, 2, 1, 0);
    RET_INFO(R16G16_UNORM, 4, 2, 0);
    RET_INFO(R16G16_SNORM, 4, 2, 0);
    RET_INFO(R16G16_USCALED, 4, 2, 0);
    RET_INFO(R16G16_SSCALED, 4, 2, 0);
    RET_INFO(R16G16_UINT, 4, 2, 0);
    RET_INFO(R16G16_SINT, 4, 2, 0);
    RET_INFO(R16G16_SFLOAT, 4, 2, 0);
    RET_INFO(R16G16B16_UNORM, 6, 3, 0);
    RET_INFO(R16G16B16_SNORM, 6, 3, 0);
    RET_INFO(R16G16B16_USCALED, 6, 3, 0);
    RET_INFO(R16G16B16_SSCALED, 6, 3, 0);
    RET_INFO(R16G16B16_UINT, 6, 3, 0);
    RET_INFO(R16G16B16_SINT, 6, 3, 0);
    RET_INFO(R16G16B16_SFLOAT, 6, 3, 0);
    RET_INFO(R16G16B16A16_UNORM, 8, 4, 0);
    RET_INFO(R16G16B16A16_SNORM, 8, 4, 0);
    RET_INFO(R16G16B16A16_USCALED, 8, 4, 0);
    RET_INFO(R16G16B16A16_SSCALED, 8, 4, 0);
    RET_INFO(R16G16B16A16_UINT, 8, 4, 0);
    RET_INFO(R16G16B16A16_SINT, 8, 4, 0);
    RET_INFO(R16G16B16A16_SFLOAT, 8, 4, 0);
    RET_INFO(R32_UINT, 4, 1, 0);
    RET_INFO(R32_SINT, 4, 1, 0);
    RET_INFO(R32_SFLOAT, 4, 1, 0);
    RET_INFO(R32G32_UINT, 8, 2, 0);
    RET_INFO(R32G32_SINT, 8, 2, 0);
    RET_INFO(R32G32_SFLOAT, 8, 2, 0);
    RET_INFO(R32G32B32_UINT, 12, 3, 0);
    RET_INFO(R32G32B32_SINT, 12, 3, 0);
    RET_INFO(R32G32B32_SFLOAT, 12, 3, 0);
    RET_INFO(R32G32B32A32_UINT, 16, 4, 0);
    RET_INFO(R32G32B32A32_SINT, 16, 4, 0);
    RET_INFO(R32G32B32A32_SFLOAT, 16, 4, 0);
    RET_INFO(R64_UINT, 8, 1, 0);
    RET_INFO(R64_SINT, 8, 1, 0);
    RET_INFO(R64_SFLOAT, 8, 1, 0);
    RET_INFO(R64G64_UINT, 16, 2, 0);
    RET_INFO(R64G64_SINT, 16, 2, 0);
    RET_INFO(R64G64_SFLOAT, 16, 2, 0);
    RET_INFO(R64G64B64_UINT, 24, 3, 0);
    RET_INFO(R64G64B64_SINT, 24, 3, 0);
    RET_INFO(R64G64B64_SFLOAT, 24, 3, 0);
    RET_INFO(R64G64B64A64_UINT, 32, 4, 0);
    RET_INFO(R64G64B64A64_SINT, 32, 4, 0);
    RET_INFO(R64G64B64A64_SFLOAT, 32, 4, 0);
    RET_INFO(B10G11R11_UFLOAT_PACK32, 4, 3, 0);
    RET_INFO(E5B9G9R9_UFLOAT_PACK32, 4, 3, 0);
    RET_INFO(D16_UNORM, 2, 1, 0);
    RET_INFO(X8_D24_UNORM_PACK32, 4, 1, 0);
    RET_INFO(D32_SFLOAT, 4, 1, 0);
    RET_INFO(S8_UINT, 1, 1, 0);
    RET_INFO(D16_UNORM_S8_UINT, 3, 2, 0);
    RET_INFO(D24_UNORM_S8_UINT, 4, 2, 0);
    RET_INFO(D32_SFLOAT_S8_UINT, 8, 2, 0);
    RET_INFO(BC1_RGB_UNORM_BLOCK, 8, 3, RvkFormat_Block4x4);
    RET_INFO(BC1_RGB_SRGB_BLOCK, 8, 3, RvkFormat_Block4x4 | RvkFormat_Srgb);
    RET_INFO(BC1_RGBA_UNORM_BLOCK, 8, 4, RvkFormat_Block4x4);
    RET_INFO(BC1_RGBA_SRGB_BLOCK, 8, 4, RvkFormat_Block4x4 | RvkFormat_Srgb);
    RET_INFO(BC2_UNORM_BLOCK, 16, 4, RvkFormat_Block4x4);
    RET_INFO(BC2_SRGB_BLOCK, 16, 4, RvkFormat_Block4x4 | RvkFormat_Srgb);
    RET_INFO(BC3_UNORM_BLOCK, 16, 4, RvkFormat_Block4x4);
    RET_INFO(BC3_SRGB_BLOCK, 16, 4, RvkFormat_Block4x4 | RvkFormat_Srgb);
    RET_INFO(BC4_UNORM_BLOCK, 8, 1, RvkFormat_Block4x4);
    RET_INFO(BC4_SNORM_BLOCK, 8, 1, RvkFormat_Block4x4);
    RET_INFO(BC5_UNORM_BLOCK, 16, 2, RvkFormat_Block4x4);
    RET_INFO(BC5_SNORM_BLOCK, 16, 2, RvkFormat_Block4x4);
    RET_INFO(BC6H_UFLOAT_BLOCK, 16, 4, RvkFormat_Block4x4);
    RET_INFO(BC6H_SFLOAT_BLOCK, 16, 4, RvkFormat_Block4x4);
    RET_INFO(BC7_UNORM_BLOCK, 16, 4, RvkFormat_Block4x4);
    RET_INFO(BC7_SRGB_BLOCK, 16, 4, RvkFormat_Block4x4 | RvkFormat_Srgb);
    RET_INFO(ETC2_R8G8B8_UNORM_BLOCK, 8, 3, RvkFormat_Block4x4);
    RET_INFO(ETC2_R8G8B8_SRGB_BLOCK, 8, 3, RvkFormat_Block4x4 | RvkFormat_Srgb);
    RET_INFO(ETC2_R8G8B8A1_UNORM_BLOCK, 8, 4, RvkFormat_Block4x4);
    RET_INFO(ETC2_R8G8B8A1_SRGB_BLOCK, 8, 4, RvkFormat_Block4x4 | RvkFormat_Srgb);
    RET_INFO(ETC2_R8G8B8A8_UNORM_BLOCK, 16, 4, RvkFormat_Block4x4 | RvkFormat_RGBA);
    RET_INFO(ETC2_R8G8B8A8_SRGB_BLOCK, 16, 4, RvkFormat_Block4x4 | RvkFormat_RGBA | RvkFormat_Srgb);
    RET_INFO(EAC_R11_UNORM_BLOCK, 8, 1, RvkFormat_Block4x4);
    RET_INFO(EAC_R11_SNORM_BLOCK, 8, 1, RvkFormat_Block4x4);
    RET_INFO(EAC_R11G11_UNORM_BLOCK, 16, 2, RvkFormat_Block4x4);
    RET_INFO(EAC_R11G11_SNORM_BLOCK, 16, 2, RvkFormat_Block4x4);
    RET_INFO(ASTC_4x4_UNORM_BLOCK, 16, 4, 0);
    RET_INFO(ASTC_4x4_SRGB_BLOCK, 16, 4, RvkFormat_Srgb);
    RET_INFO(ASTC_4x4_SFLOAT_BLOCK_EXT, 16, 4, 0);
    RET_INFO(ASTC_5x4_UNORM_BLOCK, 16, 4, 0);
    RET_INFO(ASTC_5x4_SRGB_BLOCK, 16, 4, RvkFormat_Srgb);
    RET_INFO(ASTC_5x4_SFLOAT_BLOCK_EXT, 16, 4, 0);
    RET_INFO(ASTC_5x5_UNORM_BLOCK, 16, 4, 0);
    RET_INFO(ASTC_5x5_SRGB_BLOCK, 16, 4, RvkFormat_Srgb);
    RET_INFO(ASTC_5x5_SFLOAT_BLOCK_EXT, 16, 4, 0);
    RET_INFO(ASTC_6x5_UNORM_BLOCK, 16, 4, 0);
    RET_INFO(ASTC_6x5_SRGB_BLOCK, 16, 4, RvkFormat_Srgb);
    RET_INFO(ASTC_6x5_SFLOAT_BLOCK_EXT, 16, 4, 0);
    RET_INFO(ASTC_6x6_UNORM_BLOCK, 16, 4, 0);
    RET_INFO(ASTC_6x6_SRGB_BLOCK, 16, 4, RvkFormat_Srgb);
    RET_INFO(ASTC_6x6_SFLOAT_BLOCK_EXT, 16, 4, 0);
    RET_INFO(ASTC_8x5_UNORM_BLOCK, 16, 4, 0);
    RET_INFO(ASTC_8x5_SRGB_BLOCK, 16, 4, RvkFormat_Srgb);
    RET_INFO(ASTC_8x5_SFLOAT_BLOCK_EXT, 16, 4, 0);
    RET_INFO(ASTC_8x6_UNORM_BLOCK, 16, 4, 0);
    RET_INFO(ASTC_8x6_SRGB_BLOCK, 16, 4, RvkFormat_Srgb);
    RET_INFO(ASTC_8x6_SFLOAT_BLOCK_EXT, 16, 4, 0);
    RET_INFO(ASTC_8x8_UNORM_BLOCK, 16, 4, 0);
    RET_INFO(ASTC_8x8_SRGB_BLOCK, 16, 4, RvkFormat_Srgb);
    RET_INFO(ASTC_8x8_SFLOAT_BLOCK_EXT, 16, 4, 0);
    RET_INFO(ASTC_10x5_UNORM_BLOCK, 16, 4, 0);
    RET_INFO(ASTC_10x5_SRGB_BLOCK, 16, 4, RvkFormat_Srgb);
    RET_INFO(ASTC_10x5_SFLOAT_BLOCK_EXT, 16, 4, 0);
    RET_INFO(ASTC_10x6_UNORM_BLOCK, 16, 4, 0);
    RET_INFO(ASTC_10x6_SRGB_BLOCK, 16, 4, RvkFormat_Srgb);
    RET_INFO(ASTC_10x6_SFLOAT_BLOCK_EXT, 16, 4, 0);
    RET_INFO(ASTC_10x8_UNORM_BLOCK, 16, 4, 0);
    RET_INFO(ASTC_10x8_SRGB_BLOCK, 16, 4, RvkFormat_Srgb);
    RET_INFO(ASTC_10x8_SFLOAT_BLOCK_EXT, 16, 4, 0);
    RET_INFO(ASTC_10x10_UNORM_BLOCK, 16, 4, 0);
    RET_INFO(ASTC_10x10_SRGB_BLOCK, 16, 4, RvkFormat_Srgb);
    RET_INFO(ASTC_10x10_SFLOAT_BLOCK_EXT, 16, 4, 0);
    RET_INFO(ASTC_12x10_UNORM_BLOCK, 16, 4, 0);
    RET_INFO(ASTC_12x10_SRGB_BLOCK, 16, 4, RvkFormat_Srgb);
    RET_INFO(ASTC_12x10_SFLOAT_BLOCK_EXT, 16, 4, 0);
    RET_INFO(ASTC_12x12_UNORM_BLOCK, 16, 4, 0);
    RET_INFO(ASTC_12x12_SRGB_BLOCK, 16, 4, RvkFormat_Srgb);
    RET_INFO(ASTC_12x12_SFLOAT_BLOCK_EXT, 16, 4, 0);
    RET_INFO(PVRTC1_2BPP_UNORM_BLOCK_IMG, 8, 4, 0);
    RET_INFO(PVRTC1_4BPP_UNORM_BLOCK_IMG, 8, 4, 0);
    RET_INFO(PVRTC2_2BPP_UNORM_BLOCK_IMG, 8, 4, 0);
    RET_INFO(PVRTC2_4BPP_UNORM_BLOCK_IMG, 8, 4, 0);
    RET_INFO(PVRTC1_2BPP_SRGB_BLOCK_IMG, 8, 4, RvkFormat_Srgb);
    RET_INFO(PVRTC1_4BPP_SRGB_BLOCK_IMG, 8, 4, RvkFormat_Srgb);
    RET_INFO(PVRTC2_2BPP_SRGB_BLOCK_IMG, 8, 4, RvkFormat_Srgb);
    RET_INFO(PVRTC2_4BPP_SRGB_BLOCK_IMG, 8, 4, RvkFormat_Srgb);
    RET_INFO(R10X6_UNORM_PACK16, 2, 1, 0);
    RET_INFO(R10X6G10X6_UNORM_2PACK16, 4, 2, 0);
    RET_INFO(R10X6G10X6B10X6A10X6_UNORM_4PACK16, 8, 4, 0);
    RET_INFO(R12X4_UNORM_PACK16, 2, 1, 0);
    RET_INFO(R12X4G12X4_UNORM_2PACK16, 4, 2, 0);
    RET_INFO(R12X4G12X4B12X4A12X4_UNORM_4PACK16, 8, 4, 0);
    RET_INFO(G8B8G8R8_422_UNORM, 4, 4, 0);
    RET_INFO(B8G8R8G8_422_UNORM, 4, 4, 0);
    RET_INFO(G10X6B10X6G10X6R10X6_422_UNORM_4PACK16, 8, 4, 0);
    RET_INFO(B10X6G10X6R10X6G10X6_422_UNORM_4PACK16, 8, 4, 0);
    RET_INFO(G12X4B12X4G12X4R12X4_422_UNORM_4PACK16, 8, 4, 0);
    RET_INFO(B12X4G12X4R12X4G12X4_422_UNORM_4PACK16, 8, 4, 0);
    RET_INFO(G16B16G16R16_422_UNORM, 8, 4, 0);
    RET_INFO(B16G16R16G16_422_UNORM, 8, 4, 0);
    RET_INFO(G8_B8_R8_3PLANE_420_UNORM, 6, 3, 0);
    RET_INFO(G8_B8R8_2PLANE_420_UNORM, 6, 3, 0);
    RET_INFO(G10X6_B10X6_R10X6_3PLANE_420_UNORM_3PACK16, 12, 3, 0);
    RET_INFO(G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16, 12, 3, 0);
    RET_INFO(G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16, 12, 3, 0);
    RET_INFO(G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16, 12, 3, 0);
    RET_INFO(G16_B16_R16_3PLANE_420_UNORM, 12, 3, 0);
    RET_INFO(G16_B16R16_2PLANE_420_UNORM, 12, 3, 0);
    RET_INFO(G8_B8_R8_3PLANE_422_UNORM, 4, 3, 0);
    RET_INFO(G8_B8R8_2PLANE_422_UNORM, 4, 3, 0);
    RET_INFO(G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16, 8, 3, 0);
    RET_INFO(G10X6_B10X6R10X6_2PLANE_422_UNORM_3PACK16, 8, 3, 0);
    RET_INFO(G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16, 8, 3, 0);
    RET_INFO(G12X4_B12X4R12X4_2PLANE_422_UNORM_3PACK16, 8, 3, 0);
    RET_INFO(G16_B16_R16_3PLANE_422_UNORM, 8, 3, 0);
    RET_INFO(G16_B16R16_2PLANE_422_UNORM, 8, 3, 0);
    RET_INFO(G8_B8_R8_3PLANE_444_UNORM, 3, 3, 0);
    RET_INFO(G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16, 6, 3, 0);
    RET_INFO(G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16, 6, 3, 0);
    RET_INFO(G16_B16_R16_3PLANE_444_UNORM, 6, 3, 0);
  default:
    return (RvkFormatInfo){.name = string_lit("unknown"), .size = 0, .channels = 0, .flags = 0};
  }
#undef RET_INFO
}
