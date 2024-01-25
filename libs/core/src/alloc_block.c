#include "core_alloc.h"
#include "core_bits.h"
#include "core_diag.h"
#include "core_math.h"
#include "core_thread.h"

#include "alloc_internal.h"

/**
 * Fixed size block allocator.
 * - Uses a linked list of free blocks.
 * - Once no more free blocks are available a new chunk is allocated and split into blocks.
 * - Threadsafe by protecting the apis with a basic SpinLock.
 *
 * NOTE: Memory pages are only freed to the system on destruction of the block allocator.
 */

#define main_align alignof(AllocatorBlock)
#define main_size_total (16 * usize_kibibyte)
#define main_size_useable (main_size_total - sizeof(AllocatorBlock))

#define chunk_align alignof(BlockChunk)
#define chunk_size_total (16 * usize_kibibyte)
#define chunk_size_useable (chunk_size_total - sizeof(BlockChunk))

typedef struct sBlockNode {
  struct sBlockNode* next;
} BlockNode;

typedef struct sBlockChunk {
  struct sBlockChunk* next;
} BlockChunk;

typedef struct {
  Allocator      api;
  Allocator*     parent;
  ThreadSpinLock spinLock;
  BlockNode*     freeHead;
  BlockChunk*    chunkHead;
  usize          blockSize;
  usize          allocatedBlocks;
} AllocatorBlock;

static void alloc_block_lock(AllocatorBlock* allocBlock) {
  thread_spinlock_lock(&allocBlock->spinLock);
}

static void alloc_block_unlock(AllocatorBlock* allocBlock) {
  thread_spinlock_unlock(&allocBlock->spinLock);
}

static void alloc_block_freelist_push(AllocatorBlock* allocBlock, void* blockHead) {
  BlockNode* node      = blockHead;
  *node                = (BlockNode){.next = allocBlock->freeHead};
  allocBlock->freeHead = node;

  alloc_poison(mem_create(node, allocBlock->blockSize));
}

static void alloc_block_freelist_push_many(AllocatorBlock* allocBlock, Mem chunk) {
  u8* head = bits_align_ptr(chunk.ptr, allocBlock->blockSize);
  for (; (head + allocBlock->blockSize) <= mem_end(chunk); head += allocBlock->blockSize) {
    alloc_block_freelist_push(allocBlock, head);
  }
}

static void* alloc_block_freelist_pop(AllocatorBlock* allocBlock) {
  BlockNode* node = allocBlock->freeHead;

  alloc_unpoison(mem_create(node, allocBlock->blockSize));
  allocBlock->freeHead = node->next;
  return node;
}

static bool alloc_block_chunk_create(AllocatorBlock* allocBlock) {
  const Mem chunkMem = alloc_alloc(allocBlock->parent, chunk_size_total, chunk_align);
  if (UNLIKELY(!mem_valid(chunkMem))) {
    return false;
  }

  BlockChunk* chunk     = mem_as_t(chunkMem, BlockChunk);
  *chunk                = (BlockChunk){.next = allocBlock->chunkHead};
  allocBlock->chunkHead = chunk;

  alloc_block_freelist_push_many(allocBlock, mem_consume(chunkMem, sizeof(BlockChunk)));
  return true;
}

static Mem alloc_block_alloc(Allocator* allocator, const usize size, const usize align) {
  AllocatorBlock* allocBlock = (AllocatorBlock*)allocator;

  (void)align; // All blocks are aligned to atleast the block-size.

  if (UNLIKELY(size > allocBlock->blockSize)) {
    return mem_create(null, size);
  }

  void* result;
  alloc_block_lock(allocBlock);

  if (UNLIKELY(allocBlock->freeHead == null)) {
    const bool chunkCreated = alloc_block_chunk_create(allocBlock);
    if (UNLIKELY(!chunkCreated)) {
      result = null;
      goto ret;
    }
  }
  result = alloc_block_freelist_pop(allocBlock);
  ++allocBlock->allocatedBlocks;

ret:
  alloc_block_unlock(allocBlock);
  return mem_create(result, size);
}

static void alloc_block_free(Allocator* allocator, Mem mem) {
  diag_assert(mem_valid(mem));

  alloc_tag_free(mem, AllocMemType_Normal);

  AllocatorBlock* allocBlock = (AllocatorBlock*)allocator;

  alloc_block_lock(allocBlock);
  alloc_block_freelist_push(allocBlock, mem.ptr);
  --allocBlock->allocatedBlocks;
  alloc_block_unlock(allocBlock);
}

static usize alloc_block_max_size(Allocator* allocator) {
  AllocatorBlock* allocBlock = (AllocatorBlock*)allocator;
  return allocBlock->blockSize;
}

static void alloc_block_reset(Allocator* allocator) {
  AllocatorBlock* allocBlock = (AllocatorBlock*)allocator;

  alloc_block_lock(allocBlock);

  /**
   * Recreate the free-list by free-ing all blocks on all pages.
   */

  allocBlock->freeHead        = null;
  allocBlock->allocatedBlocks = 0;

  // Free all blocks on the chunks.
  for (BlockChunk* page = allocBlock->chunkHead; page; page = page->next) {
    const Mem chunkMem = mem_create(page, chunk_size_total);
    alloc_unpoison(chunkMem); // Unpoison so we can write to it again.
    alloc_block_freelist_push_many(allocBlock, mem_consume(chunkMem, sizeof(BlockChunk)));
  }

  // Free all blocks on the main allocation.
  const Mem mainMem = mem_create(allocator, main_size_total);
  alloc_unpoison(mainMem); // Unpoison so we can write to it again.
  alloc_block_freelist_push_many(allocBlock, mem_consume(mainMem, sizeof(AllocatorBlock)));

  alloc_block_unlock(allocBlock);
}

Allocator* alloc_block_create(Allocator* parent, const usize blockSize) {
  diag_assert_msg(blockSize >= sizeof(BlockNode), "Blocksize {} is too small", fmt_int(blockSize));

  const Mem mainMem = alloc_alloc(parent, main_size_total, main_align);
  if (!mem_valid(mainMem)) {
    diag_crash_msg("BlockAllocator failed to allocate {} from parent", fmt_size(chunk_size_total));
  }

  AllocatorBlock* allocBlock = mem_as_t(mainMem, AllocatorBlock);
  *allocBlock                = (AllocatorBlock){
      .api =
          {
              .alloc   = alloc_block_alloc,
              .free    = alloc_block_free,
              .maxSize = alloc_block_max_size,
              .reset   = alloc_block_reset,
          },
      .parent    = parent,
      .blockSize = blockSize,
  };

  // Use the remaining space to create blocks.
  alloc_block_freelist_push_many(allocBlock, mem_consume(mainMem, sizeof(AllocatorBlock)));

  return (Allocator*)allocBlock;
}

void alloc_block_destroy(Allocator* allocator) {
  AllocatorBlock* allocBlock = (AllocatorBlock*)allocator;

  if (allocBlock->allocatedBlocks) {
    alloc_crash_with_msg(
        "alloc: {} blocks of size {} leaked during in block-allocator",
        fmt_int(allocBlock->allocatedBlocks),
        fmt_size(allocBlock->blockSize));
  }

  allocBlock->freeHead = null;
  for (BlockChunk* chunk = allocBlock->chunkHead; chunk;) {
    BlockChunk* toFree = chunk;
    chunk              = chunk->next;

    alloc_unpoison(mem_create(toFree, chunk_size_total));
    alloc_free(allocBlock->parent, mem_create(toFree, chunk_size_total));
  }
  allocBlock->chunkHead = null;

  alloc_unpoison(mem_create(allocator, main_size_total));
  alloc_free(allocBlock->parent, mem_create(allocator, main_size_total));
}

usize alloc_block_allocated_blocks(Allocator* allocator) {
  AllocatorBlock* allocBlock = (AllocatorBlock*)allocator;

  alloc_block_lock(allocBlock);
  const usize result = allocBlock->allocatedBlocks;
  alloc_block_unlock(allocBlock);
  return result;
}
