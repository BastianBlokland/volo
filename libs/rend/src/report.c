#include "core/alloc.h"
#include "core/math.h"
#include "rend/report.h"

typedef struct sRendReport {
  Allocator*       bumpAlloc;
  usize            size;
  RendReportEntry* entryHead;
  RendReportEntry* entryTail;
} RendReport;

typedef struct sRendReportEntry {
  RendReportType           type;
  String                   name, desc, value;
  struct sRendReportEntry* next;
} RendReportEntry;

static void rend_report_push(RendReport* report, RendReportEntry* entry) {
  if (report->entryHead) {
    report->entryTail->next = entry;
  } else {
    report->entryHead = entry;
  }
  report->entryTail = entry;
}

RendReport* rend_report_create(Allocator* alloc, const usize memCapacity) {
  const usize minSize = sizeof(RendReport) + 64 /* Minimum size for the bump allocator */;

  const Mem memTotal   = alloc_alloc(alloc, math_max(memCapacity, minSize), alignof(RendReport));
  const Mem memStorage = mem_consume(memTotal, sizeof(RendReport));

  RendReport* report = mem_as_t(memTotal, RendReport);

  *report = (RendReport){
      .bumpAlloc = alloc_bump_create(memStorage),
      .size      = memTotal.size,
  };

  return report;
}

void rend_report_destroy(Allocator* alloc, RendReport* report) {
  alloc_free(alloc, mem_create(report, report->size));
}

void rend_report_reset(RendReport* report) {
  report->entryHead = report->entryTail = null;
  alloc_reset(report->bumpAlloc);
}

const RendReportEntry* rend_report_begin(const RendReport* report) { return report->entryHead; }
const RendReportEntry* rend_report_next(const RendReportEntry* entry) { return entry->next; }

RendReportType rend_report_type(const RendReportEntry* entry) { return entry->type; }
String         rend_report_name(const RendReportEntry* entry) { return entry->name; }
String         rend_report_desc(const RendReportEntry* entry) { return entry->desc; }
String         rend_report_value(const RendReportEntry* entry) { return entry->value; }

bool rend_report_push_value(
    RendReport* report, const String name, const String desc, const String value) {

  const String nameStored = string_maybe_dup(report->bumpAlloc, name);
  if (!string_is_empty(name) && !mem_valid(nameStored)) {
    return false; // Out of space.
  }
  const String descStored = string_maybe_dup(report->bumpAlloc, desc);
  if (!string_is_empty(desc) && !mem_valid(descStored)) {
    return false; // Out of space.
  }
  const String valueStored = string_maybe_dup(report->bumpAlloc, value);
  if (!string_is_empty(value) && !mem_valid(valueStored)) {
    return false; // Out of space.
  }
  RendReportEntry* entry = alloc_alloc_t(report->bumpAlloc, RendReportEntry);
  if (!entry) {
    return false; // Out of space.
  }
  *entry = (RendReportEntry){
      .type  = RendReportType_Value,
      .name  = nameStored,
      .desc  = descStored,
      .value = valueStored,
  };
  rend_report_push(report, entry);

  return true;
}

bool rend_report_push_section(RendReport* report, const String name) {
  const String nameStored = string_maybe_dup(report->bumpAlloc, name);
  if (!string_is_empty(name) && !mem_valid(nameStored)) {
    return false; // Out of space.
  }
  RendReportEntry* entry = alloc_alloc_t(report->bumpAlloc, RendReportEntry);
  if (!entry) {
    return false; // Out of space.
  }
  *entry = (RendReportEntry){.type = RendReportType_Section, .name = nameStored};
  rend_report_push(report, entry);

  return true;
}
