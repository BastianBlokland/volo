#pragma once
#include "vulkan_api.h"

#include "forward_internal.h"

/**
 * Transferer for uploading data from the CPU to the GPU.
 * NOTE: Api is thread-safe.
 */
typedef struct sRvkTransferer RvkTransferer;

typedef enum {
  RvkTransferStatus_Busy,
  RvkTransferStatus_Finished,
} RvkTransferStatus;

typedef u64 RvkTransferId;

RvkTransferer* rvk_transferer_create(RvkDevice*);
void           rvk_transferer_destroy(RvkTransferer*);

RvkTransferId rvk_transfer_buffer(RvkTransferer*, RvkBuffer* dest, Mem data);
RvkTransferId rvk_transfer_image(RvkTransferer*, RvkImage* dest, Mem data, u32 mips, bool genMips);

RvkTransferStatus rvk_transfer_poll(const RvkTransferer*, RvkTransferId);
void              rvk_transfer_flush(RvkTransferer*); // Executes a queueSubmit.
