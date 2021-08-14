#include "cli_app.h"

#include "check_spec.h"

spec(app) {

  CliApp* app;

  setup() { app = cli_app_create(g_alloc_heap, string_lit("My test app")); }

  it("assigns unique ids to flags") {
    const CliId a = cli_register_flag(
        app, 'a', string_lit("opt-a"), string_lit("Test flag A"), CliOptionFlags_None);
    const CliId b = cli_register_flag(
        app, 'b', string_lit("opt-b"), string_lit("Test flag B"), CliOptionFlags_None);
    check_neq_int(a, b);
  }

  it("assigns unique ids to args") {
    const CliId a = cli_register_arg(app, string_lit("Test arg A"), CliOptionFlags_None);
    const CliId b = cli_register_arg(app, string_lit("Test arg B"), CliOptionFlags_None);
    check_neq_int(a, b);
  }

  it("assigns unique ids to flags and args") {
    const CliId a = cli_register_flag(
        app, 'a', string_lit("opt-a"), string_lit("Test flag"), CliOptionFlags_None);
    const CliId b = cli_register_arg(app, string_lit("Test arg"), CliOptionFlags_None);
    check_neq_int(a, b);
  }

  teardown() { cli_app_destroy(app); }
}
