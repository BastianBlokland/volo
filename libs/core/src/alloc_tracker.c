#include "core_array.h"
#include "core_bits.h"
#include "core_file.h"
#include "core_thread.h"

#include "alloc_internal.h"

#define tracker_slots_initial (16 * 1024)
#define tracker_slots_loadfactor 0.75f

typedef struct {
  Mem         mem; // mem_empty indicates that the slot is empty.
  SymbolStack stack;
} AllocTrackerSlot;

struct sAllocTracker {
  ThreadSpinLock    slotsLock;
  usize             slotCount, slotCountUsed, slotSizeUsed;
  AllocTrackerSlot* slots;
};

static bool tracker_slot_empty(const AllocTrackerSlot* slot) { return !mem_valid(slot->mem); }

static bool tracker_should_grow(AllocTracker* tracker) {
  return tracker->slotCountUsed >= (usize)(tracker->slotCount * tracker_slots_loadfactor);
}

static AllocTrackerSlot* tracker_slots_alloc(const usize slotCount) {
  const usize slotsMemSize = sizeof(AllocTrackerSlot) * slotCount;
  const Mem   slotsMem     = g_allocPage->alloc(g_allocPage, slotsMemSize, alignof(AllocTracker));
  if (UNLIKELY(!mem_valid(slotsMem))) {
    return null; // Allocation failed.
  }
  mem_set(slotsMem, 0);
  return slotsMem.ptr;
}

static AllocTrackerSlot* tracker_slot(
    AllocTrackerSlot* slots, const usize slotCount, const Mem mem, const bool includeEmpty) {
  const u64 hash   = bits_hash_64_val((u64)mem.ptr);
  usize     bucket = (usize)(hash & (slotCount - 1));
  for (usize i = 0; i != slotCount; ++i) {
    AllocTrackerSlot* slot = &slots[bucket];
    if (LIKELY(slot->mem.ptr == mem.ptr || (tracker_slot_empty(slot) && includeEmpty))) {
      return slot; // Found the right slot.
    }
    // Hash collision, jump to a new bucket (quadratic probing).
    bucket = (bucket + i + 1) & (slotCount - 1);
  }
  return null;
}

NO_INLINE_HINT static bool tracker_grow(AllocTracker* table) {
  // Allocate new slots.
  const usize       newSlotCount = bits_nextpow2(table->slotCount + 1);
  AllocTrackerSlot* newSlots     = tracker_slots_alloc(newSlotCount);
  if (UNLIKELY(!newSlots)) {
    return false; // Grow failed.
  }

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

  return true; // Grow succeeded.
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
  if (UNLIKELY(!mem_valid(mem))) {
    alloc_crash_with_msg("Invalid memory");
  }

  /**
   * NOTE: Delay crashing until we've released the spinlock, this avoids deadlocking when the
   * process of crashing requires us to allocate more memory.
   */
  enum { ErrNone, ErrDupAlloc, ErrGrowFailed };
  u32   err     = ErrNone;
  usize oldSize = 0;

  thread_spinlock_lock(&tracker->slotsLock);
  {
    AllocTrackerSlot* slot = tracker_slot(tracker->slots, tracker->slotCount, mem, true);
    if (tracker_slot_empty(slot)) {
      slot->mem   = mem;
      slot->stack = stack;
      tracker->slotCountUsed += 1;
      tracker->slotSizeUsed += mem.size;
      if (UNLIKELY(tracker_should_grow(tracker))) {
        const bool growSuccess = tracker_grow(tracker);
        if (UNLIKELY(!growSuccess)) {
          err = ErrGrowFailed;
        }
      }
    } else {
      err     = ErrDupAlloc;
      oldSize = slot->mem.size;
    }
  }
  thread_spinlock_unlock(&tracker->slotsLock);

  switch (err) {
  case ErrDupAlloc:
    diag_crash_msg(
        "Duplicate allocation (addr: {}, prev-size: {}, new-size: {}) in AllocationTracker",
        fmt_int((uptr)mem.ptr, .base = 16, .minDigits = 16),
        fmt_int(oldSize),
        fmt_int(mem.size));
    break;
  case ErrGrowFailed:
    diag_crash_msg("Failed to grow AllocationTracker (slots: {})", fmt_int(tracker->slotCount));
    break;
  }
}

void alloc_tracker_remove(AllocTracker* tracker, const Mem mem) {
  if (UNLIKELY(!mem_valid(mem))) {
    alloc_crash_with_msg("Invalid memory");
  }

  /**
   * NOTE: Delay crashing until we've released the spinlock, this avoids deadlocking when the
   * process of crashing requires us to allocate more memory.
   */
  enum { ErrNone, ErrMissing, ErrWrongSize };
  u32   err     = ErrNone;
  usize oldSize = 0;

  thread_spinlock_lock(&tracker->slotsLock);
  {
    AllocTrackerSlot* slot = tracker_slot(tracker->slots, tracker->slotCount, mem, false);
    if (UNLIKELY(!slot)) {
      err = ErrMissing;
    } else if (UNLIKELY(slot->mem.size != mem.size)) {
      err     = ErrWrongSize;
      oldSize = slot->mem.size;
    } else {
      slot->mem = mem_empty; // Mark the slot as empty.
      tracker->slotCountUsed -= 1;
      tracker->slotSizeUsed -= mem.size;
    }
  }
  thread_spinlock_unlock(&tracker->slotsLock);

  switch (err) {
  case ErrMissing:
    diag_crash_msg(
        "Allocation (addr: {}, size: {}) not found in AllocationTracker",
        fmt_int((uptr)mem.ptr, .base = 16, .minDigits = 16),
        fmt_int(mem.size));
    break;
  case ErrWrongSize:
    diag_crash_msg(
        "Allocation (addr: {}) known with a different size ({} vs {}) in AllocationTracker",
        fmt_int((uptr)mem.ptr, .base = 16, .minDigits = 16),
        fmt_int(oldSize),
        fmt_int(mem.size));
    break;
  }
}

usize alloc_tracker_count(AllocTracker* tracker) { return tracker->slotCountUsed; }
usize alloc_tracker_size(AllocTracker* tracker) { return tracker->slotSizeUsed; }

typedef struct {
  SymbolAddrRel addr; // Address in a function; not the function base address.
  u32           count;
  usize         size;
} TrackerReportEntry;

typedef struct {
  DynArray entries; // TrackerReportEntry[], sorted on addr.
} TrackerReport;

static i8 tracker_report_compare_addr(const void* a, const void* b) {
  return compare_u32(
      field_ptr(a, TrackerReportEntry, addr), field_ptr(b, TrackerReportEntry, addr));
}

static i8 tracker_report_compare_count(const void* a, const void* b) {
  const TrackerReportEntry* entryA = a;
  const TrackerReportEntry* entryB = b;
  const i8                  c      = compare_u32_reverse(&entryA->count, &entryB->count);
  return c ? c : compare_u32(&entryA->addr, &entryB->addr);
}

static TrackerReport tracker_report_create(void) {
  return (TrackerReport){.entries = dynarray_create_t(g_allocPage, TrackerReportEntry, 256)};
}

static void tracker_report_destroy(TrackerReport* report) { dynarray_destroy(&report->entries); }

static void tracker_report_add(TrackerReport* report, const SymbolAddrRel addr, const usize size) {
  TrackerReportEntry  tgt = {.addr = addr};
  TrackerReportEntry* entry =
      dynarray_find_or_insert_sorted(&report->entries, tracker_report_compare_addr, &tgt);

  entry->addr = addr;
  entry->count += 1;
  entry->size += size;
}

static void tracker_report_sort(TrackerReport* report) {
  dynarray_sort(&report->entries, tracker_report_compare_count);
}

static void tracker_report_write(TrackerReport* report, DynString* out) {
  fmt_write(out, "Active allocations (inclusive):\n");
  const SymbolAddr addrOffset = symbol_dbg_offset();
  for (u32 i = 0; i != report->entries.size; ++i) {
    const TrackerReportEntry* entry = dynarray_at_t(&report->entries, i, TrackerReportEntry);
    const SymbolAddr          entryAddrInExec = (SymbolAddr)entry->addr + addrOffset;

    const SymbolAddrRel funcAddr = symbol_dbg_base(entry->addr);
    const String        funcName = symbol_dbg_name(entry->addr);
    if (!sentinel_check(funcAddr) && !string_is_empty(funcName)) {
      const u32 offset = entry->addr - funcAddr;
      fmt_write(
          out,
          " x{>5} {>10} {} {} +{}\n",
          fmt_int(entry->count, .minDigits = 3),
          fmt_size(entry->size),
          fmt_int(entryAddrInExec, .base = 16, .minDigits = 8),
          fmt_text(funcName),
          fmt_int(offset));
    } else {
      const SymbolAddr addrAbs = symbol_addr_abs(entry->addr);
      fmt_write(
          out,
          " x{>5} {>10} {} {}\n",
          fmt_int(entry->count, .minDigits = 3),
          fmt_size(entry->size),
          fmt_int(entryAddrInExec, .base = 16, .minDigits = 8),
          fmt_int(addrAbs, .base = 16, .minDigits = 16));
    }
  }
}

void alloc_tracker_dump(AllocTracker* tracker, DynString* out) {
  TrackerReport report = tracker_report_create();

  // Aggregate the allocations in the tracker.
  thread_spinlock_lock(&tracker->slotsLock);
  {
    for (usize i = 0; i != tracker->slotCount; ++i) {
      AllocTrackerSlot* slot = &tracker->slots[i];
      if (tracker_slot_empty(slot)) {
        continue;
      }
      for (u32 frameIndex = 0; frameIndex != array_elems(slot->stack.frames); ++frameIndex) {
        const SymbolAddrRel addr = slot->stack.frames[frameIndex];
        if (sentinel_check(addr)) {
          break; // End of stack.
        }
        tracker_report_add(&report, addr, slot->mem.size);
      }
    }
  }
  thread_spinlock_unlock(&tracker->slotsLock);

  tracker_report_sort(&report);
  tracker_report_write(&report, out);
  tracker_report_destroy(&report);
}

void alloc_tracker_dump_file(AllocTracker* tracker, File* out) {
  DynString buffer = dynstring_create(g_allocPage, 4 * usize_kibibyte);
  alloc_tracker_dump(tracker, &buffer);
  file_write_sync(out, dynstring_view(&buffer));
  dynstring_destroy(&buffer);
}
