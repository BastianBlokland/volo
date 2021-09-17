#pragma once
#include "check_spec.h"

#include <setjmp.h>

#include "def_internal.h"
#include "result_internal.h"

typedef enum {
  CheckSpecContextFlags_None     = 0,
  CheckSpecContextFlags_Setup    = 1 << 0,
  CheckSpecContextFlags_Teardown = 1 << 1,
} CheckSpecContextFlags;

struct sCheckSpecContext {
  CheckTestContext* (*visitTest)(CheckSpecContext*, CheckTest);
  CheckSpecContextFlags flags;
};

struct sCheckTestContext {
  bool         started;
  CheckResult* result;
  jmp_buf      finishJumpDest; // Tests can longjmp here to early out.
};

typedef struct {
  const CheckSpecDef* def;
  DynArray            tests; // CheckTest[].
  bool                focus; // Indicates that one or more tests has focus.
} CheckSpec;

CheckSpec    check_spec_create(Allocator*, const CheckSpecDef*);
void         check_spec_destroy(CheckSpec*);
CheckResult* check_exec_test(Allocator*, const CheckSpec*, CheckTestId);
