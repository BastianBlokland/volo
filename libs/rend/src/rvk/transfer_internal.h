#pragma once
#include "vulkan_internal.h"

// Internal forward declarations:
typedef struct sRvkBuffer RvkBuffer;
typedef struct sRvkDevice RvkDevice;

typedef struct sRvkTransferer RvkTransferer;

typedef enum {
  RvkTransferStatus_Busy,
  RvkTransferStatus_Finished,
} RvkTransferStatus;

typedef u64 RvkTransferId;

RvkTransferer* rvk_transferer_create(RvkDevice*);
void           rvk_transferer_destroy(RvkTransferer*);

RvkTransferId     rvk_transfer_buffer(RvkTransferer*, RvkBuffer* dest, Mem data);
RvkTransferStatus rvk_transfer_poll(RvkTransferer*, RvkTransferId);
void              rvk_transfer_flush(RvkTransferer*);
