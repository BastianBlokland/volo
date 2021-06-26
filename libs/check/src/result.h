#pragma once
#include "core_diag.h"
#include "core_dynarray.h"

typedef enum {
  CheckResultType_None = 0,
  CheckResultType_Success,
  CheckResultType_Failure,
} CheckResultType;

typedef struct {
  String       msg;
  DiagCallSite callSite;
} CheckError;

typedef struct {
  Allocator*      alloc;
  CheckResultType type;
  DynArray        errors; // CheckError[]
} CheckResult;

CheckResult* check_result_create(Allocator*);
void         check_result_destroy(CheckResult*);
void         check_result_error(CheckResult*, String msg, DiagCallSite);
void         check_result_finish(CheckResult*, CheckResultType);
