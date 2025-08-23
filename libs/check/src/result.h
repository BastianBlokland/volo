#pragma once
#include "core/dynarray.h"
#include "core/sourceloc.h"
#include "core/time.h"

typedef struct {
  String    msg;
  SourceLoc source;
} CheckError;

typedef struct {
  Allocator*   alloc;
  bool         finished, errorsTruncated;
  TimeDuration duration;
  DynArray     errors; // CheckError[]
} CheckResult;

CheckResult* check_result_create(Allocator*);
void         check_result_destroy(CheckResult*);
void         check_result_error(CheckResult*, String msg, SourceLoc);
void         check_result_finish(CheckResult*, TimeDuration);
