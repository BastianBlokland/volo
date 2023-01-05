#include "core_array.h"
#include "core_bits.h"
#include "core_bitset.h"
#include "core_diag.h"
#include "log_logger.h"

#include "attach_internal.h"
#include "image_internal.h"

#define VOLO_RVK_ATTACH_LOGGING 1

#define rvk_attach_max_images 32
ASSERT((rvk_attach_max_images % 8) == 0, "Maximum images needs to be a multiple of 8");

typedef u32 RvkAttachIndex;

typedef enum {
  RvkAttachState_Empty   = 0, // Image has not been created.
  RvkAttachState_Busy    = 1, // Currently being rendered to.
  RvkAttachState_Pending = 2, // Will be rendered to in the next submit.
} RvkAttachState;

struct sRvkAttachPool {
  RvkDevice* device;
  u8         emptyMask[bits_to_bytes(rvk_attach_max_images)];     // Bitmask of empty slots.
  u8         availableMask[bits_to_bytes(rvk_attach_max_images)]; // Bitmask of available slots.
  u8         states[rvk_attach_max_images]; // RvkAttachState[rvk_attach_max_images].
  RvkImage   images[rvk_attach_max_images];
};

static bool rvk_attach_is_available(RvkAttachPool* pool, const RvkAttachIndex slot) {
  return bitset_test(bitset_from_array(pool->availableMask), slot);
}

static RvkAttachIndex rvk_attach_from_ptr(RvkAttachPool* pool, RvkImage* image) {
  const usize slot = image - pool->images;
  diag_assert_msg(slot < rvk_attach_max_images, "Invalid attachment pointer");
  return (RvkAttachIndex)slot;
}

static RvkAttachIndex rvk_attach_find_available(
    RvkAttachPool*           pool,
    const RvkImageType       type,
    const VkFormat           vkFormat,
    const RvkSize            size,
    const RvkImageCapability caps) {

  bitset_for(bitset_from_array(pool->availableMask), slot) {
    RvkImage* img = &pool->images[slot];
    diag_assert(img->layers == 1);
    if (img->type != type) {
      continue; // Wrong type.
    }
    if (img->vkFormat != vkFormat) {
      continue; // Wrong format.
    }
    if (img->size.data != size.data) {
      continue; // Wrong size.
    }
    if (!(img->caps & caps)) {
      continue; // Missing capability.
    }
    return (RvkAttachIndex)slot;
  }
  return sentinel_u32;
}

static RvkAttachIndex rvk_attach_create(
    RvkAttachPool*           pool,
    const RvkImageType       type,
    const VkFormat           vkFormat,
    const RvkSize            size,
    const RvkImageCapability caps) {

  const usize slot = bitset_next(bitset_from_array(pool->emptyMask), 0);
  if (sentinel_check(slot)) {
    diag_crash_msg("Maximum attachment image count ({}) exceeded", fmt_int(rvk_attach_max_images));
  }
  bitset_clear(bitset_from_array(pool->emptyMask), slot);

  switch (type) {
  case RvkImageType_ColorAttachment:
    pool->images[slot] = rvk_image_create_attach_color(pool->device, vkFormat, size, caps);
    break;
  case RvkImageType_DepthAttachment:
    pool->images[slot] = rvk_image_create_attach_depth(pool->device, vkFormat, size, caps);
    break;
  default:
    UNREACHABLE
  }
  pool->states[slot] = RvkAttachState_Pending;

#if VOLO_RVK_ATTACH_LOGGING
  log_d(
      "Vulkan attachment image created",
      log_param("slot", fmt_int(slot)),
      log_param("type", fmt_text(rvk_image_type_str(type))),
      log_param("format", fmt_text(rvk_format_info(vkFormat).name)),
      log_param("size", fmt_list_lit(fmt_int(size.width), fmt_int(size.height))));
#endif

  return (RvkAttachIndex)slot;
}

RvkImage* rvk_attach_acquire(
    RvkAttachPool*           pool,
    const RvkImageType       type,
    const VkFormat           vkFormat,
    const RvkSize            size,
    const RvkImageCapability caps) {

  RvkAttachIndex slot = rvk_attach_find_available(pool, type, vkFormat, size, caps);
  if (sentinel_check(slot)) {
    slot = rvk_attach_create(pool, type, vkFormat, size, caps);
  } else {
    pool->states[slot] = RvkAttachState_Pending;
  }
  bitset_clear(bitset_from_array(pool->availableMask), slot);
  return &pool->images[slot];
}

RvkAttachPool* rvk_attach_pool_create(RvkDevice* device) {
  RvkAttachPool* pool = alloc_alloc_t(g_alloc_heap, RvkAttachPool);
  *pool               = (RvkAttachPool){.device = device};
  bitset_set_all(bitset_from_array(pool->emptyMask), rvk_attach_max_images);
  return pool;
}

void rvk_attach_pool_destroy(RvkAttachPool* pool) {
  for (RvkAttachIndex slot = 0; slot != rvk_attach_max_images; ++slot) {
    if (pool->states[slot] != RvkAttachState_Empty) {
      rvk_image_destroy(&pool->images[slot], pool->device);
    }
  }
  alloc_free_t(g_alloc_heap, pool);
}

void rvk_attach_pool_flush(RvkAttachPool* pool) {
  for (RvkAttachIndex slot = 0; slot != rvk_attach_max_images; ++slot) {
    if (pool->states[slot] == RvkAttachState_Empty) {
      continue; // Slot was empty.
    }
    if (!rvk_attach_is_available(pool, slot)) {
      continue; // Image is still acquired.
    }

    /**
     * Update the image state and destroy the image if its no longer in use.
     */
    if (--pool->states[slot] == RvkAttachState_Empty) {
      bitset_set(bitset_from_array(pool->emptyMask), slot);
      bitset_clear(bitset_from_array(pool->availableMask), slot);
      rvk_image_destroy(&pool->images[slot], pool->device);

#if VOLO_RVK_ATTACH_LOGGING
      log_d("Vulkan attachment image destroyed", log_param("slot", fmt_int(slot)));
#endif
    }
  }
}

RvkImage* rvk_attach_acquire_color(
    RvkAttachPool*           pool,
    const VkFormat           vkFormat,
    const RvkSize            size,
    const RvkImageCapability caps) {
  return rvk_attach_acquire(pool, RvkImageType_ColorAttachment, vkFormat, size, caps);
}

RvkImage* rvk_attach_acquire_depth(
    RvkAttachPool*           pool,
    const VkFormat           vkFormat,
    const RvkSize            size,
    const RvkImageCapability caps) {
  return rvk_attach_acquire(pool, RvkImageType_DepthAttachment, vkFormat, size, caps);
}

void rvk_attach_release(RvkAttachPool* pool, RvkImage* image) {
  const RvkAttachIndex slot = rvk_attach_from_ptr(pool, image);

  // Sanity check the slot.
  diag_assert_msg(pool->states[slot] != RvkAttachState_Empty, "Attachment invalid");
  diag_assert_msg(!rvk_attach_is_available(pool, slot), "Attachment already released");

  // Mark the slot as available.
  bitset_set(bitset_from_array(pool->availableMask), slot);
}
