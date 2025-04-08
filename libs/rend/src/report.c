#include "core_alloc.h"
#include "core_math.h"
#include "rend_report.h"

typedef struct sRendReport {
  Allocator*       bumpAlloc;
  usize            size;
  RendReportEntry* entryHead;
} RendReport;

typedef struct sRendReportEntry {
  struct sRendReportEntry* next;
  String                   name, desc, value;
} RendReportEntry;

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
  report->entryHead = null;
  alloc_reset(report->bumpAlloc);
}

const RendReportEntry* rend_report_begin(const RendReport* report) { return report->entryHead; }

const RendReportEntry* rend_report_next(const RendReport* report, const RendReportEntry* entry) {
  (void)report;
  return entry->next;
}

String rend_report_name(const RendReport* report, const RendReportEntry* entry) {
  (void)report;
  return entry->name;
}

String rend_report_desc(const RendReport* report, const RendReportEntry* entry) {
  (void)report;
  return entry->desc;
}

String rend_report_value(const RendReport* report, const RendReportEntry* entry) {
  (void)report;
  return entry->value;
}

bool rend_report_push(
    RendReport* report, const String name, const String desc, const String value) {
  RendReportEntry* entry = alloc_alloc_t(report->bumpAlloc, RendReportEntry);
  if (!entry) {
    return false; // Out of space.
  }
  entry->name  = string_dup(report->bumpAlloc, name);
  entry->desc  = string_maybe_dup(report->bumpAlloc, desc);
  entry->value = string_dup(report->bumpAlloc, value);

  if (report->entryHead) {
    report->entryHead->next = entry;
  } else {
    report->entryHead = entry;
  }
  return true;
}
