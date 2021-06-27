#pragma once
#include "check_def.h"

/**
 * TestRun result.
 */
typedef enum {
  CheckResultType_Pass = 0,
  CheckResultType_Fail = 1,
} CheckResultType;

/**
 * Run the given TestSuite definition.
 */
CheckResultType check_run(CheckDef*);
