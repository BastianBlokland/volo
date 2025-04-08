#pragma once
#include "core.h"
#include "core_string.h"

typedef struct sRendReport      RendReport;
typedef struct sRendReportEntry RendReportEntry;

RendReport* rend_report_create(Allocator*, usize memCapacity);
void        rend_report_destroy(Allocator*, RendReport*);
void        rend_report_reset(RendReport*);

const RendReportEntry* rend_report_begin(const RendReport*);
const RendReportEntry* rend_report_next(const RendReport*, const RendReportEntry*);

String rend_report_name(const RendReport*, const RendReportEntry*);
String rend_report_desc(const RendReport*, const RendReportEntry*);
String rend_report_value(const RendReport*, const RendReportEntry*);

bool rend_report_push(RendReport*, String name, String desc, String value);
