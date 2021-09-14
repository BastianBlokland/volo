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

  it("supports registering exclusions") {
    const CliId a = cli_register_flag(app, 'a', string_lit("opt-a"), CliOptionFlags_None);
    const CliId b = cli_register_flag(app, 'b', string_lit("opt-b"), CliOptionFlags_None);
    const CliId c = cli_register_flag(app, 'c', string_lit("opt-c"), CliOptionFlags_None);

    cli_register_exclusion(app, a, b);

    check(cli_excludes(app, a, b));
    check(!cli_excludes(app, a, c));
    check(!cli_excludes(app, b, c));
  }

  it("supports registering descriptions for options") {
    const CliId a = cli_register_flag(app, 'a', string_lit("opt-a"), CliOptionFlags_None);
    const CliId b = cli_register_arg(app, string_lit("arg-1"), CliOptionFlags_None);
    const CliId c = cli_register_arg(app, string_lit("arg-2"), CliOptionFlags_None);

    cli_register_desc(app, a, string_lit("A nice flag"));
    cli_register_desc(app, b, string_lit("A nice argument"));

    check_eq_string(cli_desc(app, a), string_lit("A nice flag"));
    check_eq_string(cli_desc(app, b), string_lit("A nice argument"));
    check_eq_string(cli_desc(app, c), string_empty);
  }

  it("supports descriptions with preset choices") {
    static const String choices[] = {
        string_static("ChoiceA"),
        string_static("ChoiceB"),
        string_static("ChoiceC"),
    };

    const CliId a = cli_register_flag(app, 'a', string_lit("opt-a"), CliOptionFlags_None);
    const CliId b = cli_register_flag(app, 'b', string_lit("opt-b"), CliOptionFlags_None);
    const CliId c = cli_register_flag(app, 'c', string_lit("opt-c"), CliOptionFlags_None);
    const CliId d = cli_register_flag(app, 'd', string_lit("opt-d"), CliOptionFlags_None);

    cli_register_desc_choice_array(app, a, string_empty, choices, sentinel_usize);
    cli_register_desc_choice_array(app, b, string_lit("A nice flag."), choices, sentinel_usize);
    cli_register_desc_choice_array(app, c, string_lit("A nice flag."), choices, 0);
    cli_register_desc_choice_array(app, d, string_lit("A nice flag."), choices, 2);

    check_eq_string(cli_desc(app, a), string_lit("Options: 'ChoiceA', 'ChoiceB', 'ChoiceC'."));
    check_eq_string(
        cli_desc(app, b), string_lit("A nice flag. Options: 'ChoiceA', 'ChoiceB', 'ChoiceC'."));
    check_eq_string(
        cli_desc(app, c),
        string_lit("A nice flag. Options: 'ChoiceA', 'ChoiceB', 'ChoiceC'. Default: 'ChoiceA'."));
    check_eq_string(
        cli_desc(app, d),
        string_lit("A nice flag. Options: 'ChoiceA', 'ChoiceB', 'ChoiceC'. Default: 'ChoiceC'."));
  }

  teardown() { cli_app_destroy(app); }
}
