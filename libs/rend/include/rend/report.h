#pragma once
#include "core/forward.h"
#include "core/string.h"

typedef struct sRendReport      RendReport;
typedef struct sRendReportEntry RendReportEntry;

typedef enum {
  RendReportType_Value,
  RendReportType_Section,
} RendReportType;

RendReport* rend_report_create(Allocator*, usize memCapacity);
void        rend_report_destroy(Allocator*, RendReport*);
void        rend_report_reset(RendReport*);

const RendReportEntry* rend_report_begin(const RendReport*);
const RendReportEntry* rend_report_next(const RendReportEntry*);

RendReportType rend_report_type(const RendReportEntry*);
String         rend_report_name(const RendReportEntry*);
String         rend_report_desc(const RendReportEntry*);
String         rend_report_value(const RendReportEntry*);

bool rend_report_push_value(RendReport*, String name, String desc, String value);
bool rend_report_push_section(RendReport*, String name);
