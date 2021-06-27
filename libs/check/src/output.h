#pragma once
#include "check_runner.h"

#include "spec_internal.h"

typedef struct sCheckOutput CheckOutput;

struct sCheckOutput {
  void (*runStarted)(CheckOutput*);

  void (*testsDiscovered)(CheckOutput*, usize count, TimeDuration);

  void (*testFinished)(
      CheckOutput*, const CheckSpec*, const CheckBlock*, CheckResultType, CheckResult*);

  void (*runFinished)(
      CheckOutput*,
      CheckResultType,
      TimeDuration,
      usize numPassed,
      usize numFailed,
      usize numSkipped);

  void (*destroy)(CheckOutput*);
};

void check_output_destroy(CheckOutput*);
