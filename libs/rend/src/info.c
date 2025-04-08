#include "core_alloc.h"
#include "core_math.h"
#include "rend_info.h"

typedef struct sRendInfo {
  Allocator*     bumpAlloc;
  usize          size;
  RendInfoEntry* entryHead;
} RendInfo;

RendInfo* rend_info_create(Allocator* alloc, const usize memCapacity) {
  const usize minSize = sizeof(RendInfo) + 64 /* Minimum size for the bump allocator */;

  const Mem memTotal   = alloc_alloc(alloc, math_max(memCapacity, minSize), alignof(RendInfo));
  const Mem memStorage = mem_consume(memTotal, sizeof(RendInfo));

  RendInfo* info = mem_as_t(memTotal, RendInfo);

  *info = (RendInfo){
      .bumpAlloc = alloc_bump_create(memStorage),
      .size      = memTotal.size,
  };

  return info;
}

void rend_info_destroy(Allocator* alloc, RendInfo* info) {
  alloc_free(alloc, mem_create(info, info->size));
}

void rend_info_reset(RendInfo* info) {
  info->entryHead = null;
  alloc_reset(info->bumpAlloc);
}

const RendInfoEntry* rend_info_begin(const RendInfo* info) { return info->entryHead; }

bool rend_info_push(RendInfo* info, const String name, const String desc, const String value) {
  RendInfoEntry* entry = alloc_alloc_t(info->bumpAlloc, RendInfoEntry);
  if (!entry) {
    return false; // Out of space.
  }
  entry->name  = string_dup(info->bumpAlloc, name);
  entry->desc  = string_maybe_dup(info->bumpAlloc, desc);
  entry->value = string_dup(info->bumpAlloc, value);

  if (info->entryHead) {
    info->entryHead->next = entry;
  } else {
    info->entryHead = entry;
  }
  return true;
}
