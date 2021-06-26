#pragma once
#include "check_def.h"

/**
 * Test run result code.
 */
typedef enum {
  CheckRunResult_Success = 0,
  CheckRunResult_Failure = 1,
} CheckRunResult;

/**
 * TODO: Document
 */
CheckRunResult check_run(CheckDef*);
