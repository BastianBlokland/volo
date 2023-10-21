#include "app_cli.h"
#include "core_file.h"
#include "core_signal.h"
#include "core_thread.h"
#include "core_time.h"

/**
 * Helper executable that is used in the process tests.
 */

static CliId g_optExitCode, g_optBlock, g_optWait;

void app_cli_configure(CliApp* app) {
  g_optExitCode = cli_register_flag(app, 0, string_lit("exitcode"), CliOptionFlags_Value);
  g_optBlock    = cli_register_flag(app, 0, string_lit("block"), CliOptionFlags_None);
  g_optWait     = cli_register_flag(app, 0, string_lit("wait"), CliOptionFlags_None);
}

i32 app_cli_run(const CliApp* app, const CliInvocation* invoc) {
  (void)app;

  if (cli_parse_provided(invoc, g_optBlock)) {
    while (true) {
      thread_sleep(time_second);
    }
  }

  if (cli_parse_provided(invoc, g_optWait)) {
    signal_intercept_enable();
    while (!signal_is_received(Signal_Interrupt)) {
      thread_yield();
    }
  }

  return (i32)cli_read_i64(invoc, g_optExitCode, 0);
}
