#include "core_diag.h"
#include "core_file.h"
#include "core_math.h"
#include "core_symbol.h"

#include "diag_internal.h"

#define VOLO_DIAG_CRASH_REPORT

static THREAD_LOCAL bool          g_diagIsCrashing;
static THREAD_LOCAL AssertHandler g_assertHandler;
static THREAD_LOCAL void*         g_assertHandlerContext;

NO_INLINE_HINT static void diag_crash_report_internal(const SymbolStack* stack, const String msg) {
  DynString str = dynstring_create_over(mem_stack(2048));
  dynstring_append(&str, string_slice(msg, 0, math_min(msg.size, 512)));

  symbol_stack_write(stack, &str);
  file_write_sync(g_file_stderr, dynstring_view(&str));
}

INLINE_HINT NORETURN static void diag_crash_internal(MAYBE_UNUSED const String msg) {
  if (!g_diagIsCrashing) {
    /**
     * Handle a crash happening while in this function.
     * Can for example happen when an error occurs while resolving stack symbol names.
     */
    g_diagIsCrashing = true;

#ifdef VOLO_DIAG_CRASH_REPORT
    const SymbolStack stack = symbol_stack_walk();
    diag_crash_report_internal(&stack, msg);
#else
    (void)diag_crash_report_internal;
#endif

    diag_pal_break();
  }
  diag_pal_crash();
}

void diag_print_raw(const String userMsg) { file_write_sync(g_file_stdout, userMsg); }
void diag_print_err_raw(const String userMsg) { file_write_sync(g_file_stderr, userMsg); }

void diag_assert_report_fail(const String userMsg, const SourceLoc sourceLoc) {
  if (g_assertHandler && g_assertHandler(userMsg, sourceLoc, g_assertHandlerContext)) {
    return; // Assert was handled.
  }
  const String msg = fmt_write_scratch(
      "Assertion failed: '{}' [file: {} line: {}]\n",
      fmt_text(userMsg),
      fmt_path(sourceLoc.file),
      fmt_int(sourceLoc.line));
  diag_crash_internal(msg);
}

void diag_break(void) { diag_pal_break(); }
void diag_crash(void) { diag_crash_internal(string_lit("Crash")); }

void diag_crash_msg_raw(const String userMsg) {
  const String msg = fmt_write_scratch("Crash: '{}'\n", fmt_text(userMsg));
  diag_crash_internal(msg);
}

void diag_set_assert_handler(AssertHandler handler, void* context) {
  g_assertHandler        = handler;
  g_assertHandlerContext = context;
}
