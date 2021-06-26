#include "core_alloc.h"
#include "core_diag.h"
#include "core_init.h"

#include "check_runner.h"

#include "spec_internal.h"

CheckRunResult check_run(CheckDef* check) {
  (void)check;
  diag_print("Hello world\n");

  dynarray_for_t(&check->specs, CheckSpecDef, specDef, {
    CheckSpec spec = check_spec_create(g_alloc_heap, specDef);

    diag_print("{}:\n", fmt_text(specDef->name));
    dynarray_for_t(&spec.blocks, CheckBlock, block, {
      diag_print(" {} - {}\n", fmt_int(block->id, .minDigits = 2), fmt_text(block->description));

      CheckResult* result = check_exec_block(g_alloc_heap, &spec, block->id);
      diag_print("  > {} ({})\n", fmt_int(result->type), fmt_duration(result->duration));

      dynarray_for_t(&result->errors, CheckError, err, {
        diag_print(
            "  {}{}{}\n",
            fmt_ttystyle(.bgColor = TtyBgColor_Red),
            fmt_text(err->msg),
            fmt_ttystyle());
      });

      check_result_destroy(result);
    });

    check_spec_destroy(&spec);
  });

  return CheckRunResult_Success;
}
