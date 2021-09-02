#include "cli_app.h"

#include "check_spec.h"

spec(app) {

  CliApp* app = null;

  setup() { app = cli_app_create(g_alloc_heap, string_lit("My test app")); }

  it("assigns unique ids to flags") {
    const CliId a = cli_register_flag(app, 'a', string_lit("opt-a"), CliOptionFlags_None);
    const CliId b = cli_register_flag(app, 'b', string_lit("opt-b"), CliOptionFlags_None);
    check(a != b);
  }

  it("assigns unique ids to args") {
    const CliId a = cli_register_arg(app, string_lit("arg-a"), CliOptionFlags_None);
    const CliId b = cli_register_arg(app, string_lit("arg-b"), CliOptionFlags_None);
    check(a != b);
  }

  it("assigns unique ids to flags and args") {
    const CliId a = cli_register_flag(app, 'a', string_lit("opt-a"), CliOptionFlags_None);
    const CliId b = cli_register_arg(app, string_lit("arg"), CliOptionFlags_None);
    check(a != b);
  }

  it("supports registering descriptions for options") {
    const CliId a = cli_register_flag(app, 'a', string_lit("opt-a"), CliOptionFlags_None);
    const CliId b = cli_register_arg(app, string_lit("arg"), CliOptionFlags_None);

    cli_register_desc(app, a, string_lit("A nice flag"));
    cli_register_desc(app, b, string_lit("A nice argument"));
  }

  teardown() { cli_app_destroy(app); }
}
