#include "core_alloc.h"
#include "core_file.h"
#include "core_math.h"
#include "core_memory.h"
#include "core_path.h"
#include "log_logger.h"

#include "device_internal.h"
#include "psocache_internal.h"

#define rvk_psocache_size_max (32 * usize_mebibyte)

/**
 * Pipeline cache header.
 * See spec, table 12:
 * https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/vkspec.html#VkPipelineCache
 */
typedef struct {
  u32 vendorId;
  u32 deviceId;
  u8  cacheId[VK_UUID_SIZE];
} RvkPsoCacheHeader;

static String rvk_psocache_path_scratch() {
  const String fileName = fmt_write_scratch("{}.psoc", fmt_text(path_stem(g_path_executable)));
  return path_build_scratch(path_parent(g_path_executable), fileName);
}

static VkPipelineCache rvk_vkcache_create(RvkDevice* dev, String data) {
  const VkPipelineCacheCreateInfo createInfo = {
      .sType           = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
      .initialDataSize = data.size,
      .pInitialData    = data.ptr,
  };
  VkPipelineCache result;
  rvk_call(vkCreatePipelineCache, dev->vkDev, &createInfo, &dev->vkAlloc, &result);
  return result;
}

static bool rvk_psocache_verify(RvkDevice* dev, const RvkPsoCacheHeader* header) {
  if (header->vendorId != dev->vkProperties.vendorID) {
    return false;
  }
  if (header->deviceId != dev->vkProperties.deviceID) {
    return false;
  }
  return mem_eq(mem_var(header->cacheId), mem_var(dev->vkProperties.pipelineCacheUUID));
}

static bool rvk_psocache_header_load(Mem input, RvkPsoCacheHeader* out) {
  const usize expectedHeaderSize = 16 + VK_UUID_SIZE;
  if (input.size < expectedHeaderSize) {
    return false;
  }
  u32 headerSize;
  input = mem_consume_le_u32(input, &headerSize);
  if (headerSize != expectedHeaderSize) {
    return false;
  }
  u32 headerVersion;
  input = mem_consume_le_u32(input, &headerVersion);
  if (headerVersion != VK_PIPELINE_CACHE_HEADER_VERSION_ONE) {
    return false;
  }
  input = mem_consume_le_u32(input, &out->vendorId);
  input = mem_consume_le_u32(input, &out->deviceId);
  mem_cpy(mem_var(out->cacheId), mem_slice(input, 0, VK_UUID_SIZE));
  return true;
}

VkPipelineCache rvk_psocache_load(RvkDevice* dev) {
  const String path = rvk_psocache_path_scratch();
  String       data = string_empty;
  File*        file = null;
  if (file_create(g_alloc_heap, path, FileMode_Open, FileAccess_Read, &file)) {
    log_i("Vulkan pipeline cache created", log_param("path", fmt_path(path)));
    goto Done;
  }
  if (file_map(file, &data)) {
    log_w("Failed to map Vulkan pipeline cache", log_param("path", fmt_path(path)));
    goto Done;
  }
  RvkPsoCacheHeader header;
  if (!rvk_psocache_header_load(data, &header)) {
    log_w("Vulkan pipeline cache corrupt", log_param("path", fmt_path(path)));
    data = string_empty;
    goto Done;
  }
  if (!rvk_psocache_verify(dev, &header)) {
    log_w("Vulkan pipeline cache incompatible", log_param("path", fmt_path(path)));
    data = string_empty;
    goto Done;
  }

  log_i(
      "Vulkan Pipeline cache loaded",
      log_param("path", fmt_path(path)),
      log_param("size", fmt_size(data.size)),
      log_param("vendor", fmt_text(rvk_vendor_str(header.vendorId))),
      log_param("device", fmt_int(header.deviceId)));

Done:
  (void)0;
  VkPipelineCache result = rvk_vkcache_create(dev, data);
  if (file) {
    file_destroy(file);
  }
  return result;
}

void rvk_psocache_save(RvkDevice* dev, VkPipelineCache vkCache) {
  usize size;
  vkGetPipelineCacheData(dev->vkDev, vkCache, &size, null);
  size = math_min(size, rvk_psocache_size_max); // Limit the maximum cache size.

  const Mem buffer = alloc_alloc(g_alloc_heap, size, 1);
  vkGetPipelineCacheData(dev->vkDev, vkCache, &size, buffer.ptr);

  const String     path = rvk_psocache_path_scratch();
  const FileResult res  = file_write_to_path_sync(path, mem_create(buffer.ptr, size));

  alloc_free(g_alloc_heap, buffer);

  if (res) {
    log_w(
        "Failed to save Vulkan pipeline cache",
        log_param("error", fmt_text(file_result_str(res))),
        log_param("path", fmt_path(path)),
        log_param("size", fmt_size(size)));
  } else {
    log_i(
        "Vulkan pipeline cache saved",
        log_param("path", fmt_path(path)),
        log_param("size", fmt_size(size)));
  }
}
