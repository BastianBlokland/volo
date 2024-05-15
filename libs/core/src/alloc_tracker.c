#include "core_bits.h"
#include "core_thread.h"

#include "alloc_internal.h"

#define tracker_slots_initial 1024
#define tracker_slots_loadfactor 0.75f

typedef struct {
  Mem         mem; // mem_empty indicates that the slot is empty.
  SymbolStack stack;
} AllocTrackerSlot;

struct sAllocTracker {
  ThreadSpinLock    slotsLock;
  usize             slotCount, slotCountUsed;
  AllocTrackerSlot* slots;
};

/**
 * SplitMix64 hash routine.
 * Reference:
 * - https://xorshift.di.unimi.it/splitmix64.c
 * - http://zimbry.blogspot.com/2011/09/better-bit-mixing-improving-on.html
 */
static u64 tracker_hash(const Mem mem) {
  u64 hash = (uptr)mem.ptr;
  hash     = (hash ^ (hash >> 30)) * u64_lit(0xbf58476d1ce4e5b9);
  hash     = (hash ^ (hash >> 27)) * u64_lit(0x94d049bb133111eb);
  hash     = hash ^ (hash >> 31);
  return hash;
}

static bool tracker_slot_empty(const AllocTrackerSlot* slot) { return !mem_valid(slot->mem); }

static bool tracker_should_grow(AllocTracker* tracker) {
  return tracker->slotCountUsed >= (usize)(tracker->slotCount * tracker_slots_loadfactor);
}

static AllocTrackerSlot* tracker_slots_alloc(const usize slotCount) {
  const usize slotsMemSize = sizeof(AllocTrackerSlot) * slotCount;
  const Mem   slotsMem     = alloc_alloc(g_allocPage, slotsMemSize, alignof(AllocTracker));
  mem_set(slotsMem, 0);
  return slotsMem.ptr;
}

static AllocTrackerSlot* tracker_slot(
    AllocTrackerSlot* slots, const usize slotCount, const Mem mem, const bool includeEmpty) {
  if (UNLIKELY(!mem_valid(mem))) {
    alloc_crash_with_msg("Invalid memory");
  }
  const u64 hash   = tracker_hash(mem);
  usize     bucket = (usize)(hash & (slotCount - 1));
  for (usize i = 0; i != slotCount; ++i) {
    AllocTrackerSlot* slot = &slots[bucket];
    if (LIKELY(slot->mem.ptr == mem.ptr || (tracker_slot_empty(slot) && includeEmpty))) {
      return slot; // Found the right slot.
    }
    // Hash collision, jump to a new bucket (quadratic probing).
    bucket = (bucket + i + 1) & (slotCount - 1);
  }
  alloc_crash_with_msg("Allocation not found in AllocTracker");
}

NO_INLINE_HINT static void tracker_grow(AllocTracker* table) {
  // Allocate new slots.
  const usize       newSlotCount = bits_nextpow2(table->slotCount + 1);
  AllocTrackerSlot* newSlots     = tracker_slots_alloc(newSlotCount);

  // Insert the old data into the new slots.
  for (AllocTrackerSlot* slot = table->slots; slot != (table->slots + table->slotCount); ++slot) {
    if (!tracker_slot_empty(slot)) {
      AllocTrackerSlot* newSlot = tracker_slot(newSlots, newSlotCount, slot->mem, true);
      *newSlot                  = *slot;
    }
  }

  // Free the old slots.
  alloc_free_array_t(g_allocPage, table->slots, table->slotCount);
  table->slots     = newSlots;
  table->slotCount = newSlotCount;
}

AllocTracker* alloc_tracker_create() {
  /**
   * NOTE: Its wasteful to use the page-allocator as it always rounds up to a whole page, however we
   * do not want to depend on any other allocators as this would limit the use of the tracker.
   */
  AllocTracker* tracker = alloc_alloc_t(g_allocPage, AllocTracker);

  diag_assert(bits_ispow2_32(tracker_slots_initial));

  *tracker = (AllocTracker){
      .slotCount = tracker_slots_initial,
      .slots     = tracker_slots_alloc(tracker_slots_initial),
  };
  return tracker;
}

void alloc_tracker_destroy(AllocTracker* tracker) {
  alloc_free_array_t(g_allocPage, tracker->slots, tracker->slotCount);
  alloc_free_t(g_allocPage, tracker);
}

void alloc_tracker_add(AllocTracker* tracker, const Mem mem, const SymbolStack stack) {
  thread_spinlock_lock(&tracker->slotsLock);
  {
    AllocTrackerSlot* slot = tracker_slot(tracker->slots, tracker->slotCount, mem, true);
    if (tracker_slot_empty(slot)) {
      slot->mem   = mem;
      slot->stack = stack;
      ++tracker->slotCountUsed;
      if (UNLIKELY(tracker_should_grow(tracker))) {
        tracker_grow(tracker);
      }
    } else {
      alloc_crash_with_msg("Duplicate allocation in AllocationTracker");
    }
  }
  thread_spinlock_unlock(&tracker->slotsLock);
}

void alloc_tracker_remove(AllocTracker* tracker, const Mem mem) {
  thread_spinlock_lock(&tracker->slotsLock);
  {
    AllocTrackerSlot* slot = tracker_slot(tracker->slots, tracker->slotCount, mem, false);
    if (UNLIKELY(slot->mem.size != mem.size)) {
      alloc_crash_with_msg("Allocation known with different size in AllocationTracker");
    }
    slot->mem = mem_empty; // Mark the slot as empty.
    --tracker->slotCountUsed;
  }
  thread_spinlock_unlock(&tracker->slotsLock);
}

void alloc_tracker_dump(File*);
