#pragma once
#include "core_dynarray.h"
#include "core_sourceloc.h"
#include "core_time.h"

typedef enum {
  CheckResultType_None = 0,
  CheckResultType_Success,
  CheckResultType_Failure,
} CheckResultType;

typedef struct {
  String    msg;
  SourceLoc source;
} CheckError;

typedef struct {
  Allocator*      alloc;
  CheckResultType type;
  TimeDuration    duration;
  DynArray        errors; // CheckError[]
} CheckResult;

CheckResult* check_result_create(Allocator*);
void         check_result_destroy(CheckResult*);
void         check_result_error(CheckResult*, String msg, SourceLoc);
void         check_result_finish(CheckResult*, CheckResultType, TimeDuration);
