#include "core_array.h"
#include "core_bits.h"
#include "core_bitset.h"
#include "core_diag.h"
#include "log_logger.h"

#include "attach_internal.h"
#include "debug_internal.h"
#include "device_internal.h"
#include "image_internal.h"

#define VOLO_RVK_ATTACH_LOGGING 0

#define rvk_attach_max_images 64
ASSERT((rvk_attach_max_images % 8) == 0, "Maximum images needs to be a multiple of 8");

// clang-format off

/**
 * Capabilities that all attachments will have.
 * TODO: Investigate if these have any (serious) performance impact.
 */
static const RvkImageCapability g_attachDefaultCapabilities =
    RvkImageCapability_TransferSource |
    RvkImageCapability_TransferDest   |
    RvkImageCapability_BlitDest       |
    RvkImageCapability_Sampled;

// clang-format on

typedef u32 RvkAttachIndex;

typedef enum {
  RvkAttachState_Empty     = 0, // Image has not been created.
  RvkAttachState_Busy      = 1, // Currently being rendered to.
  RvkAttachState_Submitted = 2, // Submitted to the gpu.
  RvkAttachState_Pending   = 3, // Will be rendered to in the next submit.
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

static RvkAttachIndex rvk_attach_from_ptr(RvkAttachPool* pool, const RvkImage* img) {
  const usize slot = img - pool->images;
  diag_assert_msg(slot < rvk_attach_max_images, "Invalid attachment pointer");
  return (RvkAttachIndex)slot;
}

static RvkAttachIndex rvk_attach_find_available(
    RvkAttachPool* pool, const RvkImageType type, const RvkAttachSpec spec, const RvkSize size) {

  bitset_for(bitset_from_array(pool->availableMask), slot) {
    RvkImage* img = &pool->images[slot];
    diag_assert(img->layers == 1);
    if (img->type != type) {
      continue; // Wrong type.
    }
    if (img->vkFormat != spec.vkFormat) {
      continue; // Wrong format.
    }
    if ((img->caps & spec.capabilities) != spec.capabilities) {
      continue; // Missing capability.
    }
    if (img->size.data != size.data) {
      continue; // Wrong size.
    }
    return (RvkAttachIndex)slot;
  }
  return sentinel_u32;
}

static RvkAttachIndex rvk_attach_create(
    RvkAttachPool* pool, const RvkImageType type, const RvkAttachSpec spec, const RvkSize size) {

  const usize slot = bitset_next(bitset_from_array(pool->emptyMask), 0);
  if (sentinel_check(slot)) {
    diag_crash_msg("Maximum attachment image count ({}) exceeded", fmt_int(rvk_attach_max_images));
  }
  bitset_clear(bitset_from_array(pool->emptyMask), slot);

  const RvkImageCapability capabilities = spec.capabilities | g_attachDefaultCapabilities;

  MAYBE_UNUSED String typeName;
  switch (type) {
  case RvkImageType_ColorAttachment:
    typeName = string_lit("color");
    pool->images[slot] =
        rvk_image_create_attach_color(pool->device, spec.vkFormat, size, capabilities);
    break;
  case RvkImageType_DepthAttachment:
    typeName = string_lit("depth");
    pool->images[slot] =
        rvk_image_create_attach_depth(pool->device, spec.vkFormat, size, capabilities);
    break;
  default:
    UNREACHABLE
  }
  pool->states[slot] = RvkAttachState_Pending;

  RvkImage* img = &pool->images[slot];
  RvkDebug* dbg = pool->device->debug;
  rvk_debug_name_img(dbg, img->vkImage, "attach_{}_{}", fmt_int(slot), fmt_text(typeName));
  rvk_debug_name_img_view(dbg, img->vkImageView, "attach_{}_{}", fmt_int(slot), fmt_text(typeName));

#if VOLO_RVK_ATTACH_LOGGING
  log_d(
      "Vulkan attachment image created",
      log_param("slot", fmt_int(slot)),
      log_param("type", fmt_text(rvk_image_type_str(type))),
      log_param("format", fmt_text(rvk_format_info(spec.vkFormat).name)),
      log_param("size", fmt_list_lit(fmt_int(size.width), fmt_int(size.height))));
#endif

  return (RvkAttachIndex)slot;
}

RvkImage* rvk_attach_acquire(
    RvkAttachPool* pool, const RvkImageType type, const RvkAttachSpec spec, const RvkSize size) {
  diag_assert_msg(size.width && size.height, "Zero sized attachments are not supported");

  RvkAttachIndex slot = rvk_attach_find_available(pool, type, spec, size);
  if (sentinel_check(slot)) {
    slot = rvk_attach_create(pool, type, spec, size);
  } else {
    pool->states[slot] = RvkAttachState_Pending;
  }
  bitset_clear(bitset_from_array(pool->availableMask), slot);
  return &pool->images[slot];
}

RvkAttachPool* rvk_attach_pool_create(RvkDevice* device) {
  RvkAttachPool* pool = alloc_alloc_t(g_allocHeap, RvkAttachPool);
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
  alloc_free_t(g_allocHeap, pool);
}

u16 rvk_attach_pool_count(const RvkAttachPool* pool) {
  return rvk_attach_max_images - (u16)bitset_count(bitset_from_array(pool->emptyMask));
}

u64 rvk_attach_pool_memory(const RvkAttachPool* pool) {
  u64 mem = 0;
  for (RvkAttachIndex slot = 0; slot != rvk_attach_max_images; ++slot) {
    if (pool->states[slot] != RvkAttachState_Empty) {
      mem += pool->images[slot].mem.size;
    }
  }
  return mem;
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

RvkImage*
rvk_attach_acquire_color(RvkAttachPool* pool, const RvkAttachSpec spec, const RvkSize size) {
  return rvk_attach_acquire(pool, RvkImageType_ColorAttachment, spec, size);
}

RvkImage*
rvk_attach_acquire_depth(RvkAttachPool* pool, const RvkAttachSpec spec, const RvkSize size) {
  return rvk_attach_acquire(pool, RvkImageType_DepthAttachment, spec, size);
}

void rvk_attach_release(RvkAttachPool* pool, RvkImage* img) {
  const RvkAttachIndex slot = rvk_attach_from_ptr(pool, img);

  // Discard the contents.
  rvk_image_transition_external(img, RvkImagePhase_Undefined);

  // Sanity check the slot.
  diag_assert_msg(pool->states[slot] != RvkAttachState_Empty, "Attachment invalid");
  diag_assert_msg(!rvk_attach_is_available(pool, slot), "Attachment already released");

  // Mark the slot as available.
  bitset_set(bitset_from_array(pool->availableMask), slot);
}
