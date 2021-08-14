#include "cli_app.h"
#include "cli_parse.h"

#include "check_spec.h"

static void parse_check_success(CheckTestContext* _testCtx, CliInvocation* invoc) {
  check_eq_int(cli_parse_result(invoc), CliParseResult_Success);

  for (usize i = 0; i != cli_parse_errors(invoc).count; ++i) {
    check_eq_string(cli_parse_errors(invoc).head[i], string_empty);
  }
}

static void parse_check_fail(
    CheckTestContext* _testCtx, CliInvocation* invoc, String* errHead, usize errCount) {
  check_eq_int(cli_parse_result(invoc), CliParseResult_Fail);

  check_eq_int(cli_parse_errors(invoc).count, errCount);
  const usize errsToCheck = math_min(errCount, cli_parse_errors(invoc).count);
  for (usize i = 0; i != errsToCheck; ++i) {
    check_eq_string(cli_parse_errors(invoc).head[i], errHead[i]);
  }
}

spec(parse) {

  CliApp* app;
  CliId   flagA, flagB, flagC, flagD, argA, argB;

  setup() {
    app   = cli_app_create(g_alloc_heap, string_lit("My test app"));
    flagA = cli_register_flag(app, 'a', string_lit("flag-a-req"), CliOptionFlags_Required);
    flagB = cli_register_flag(app, 'b', string_lit("flag-b-opt"), CliOptionFlags_None);
    flagC = cli_register_flag(app, 'c', string_lit("flag-c-val"), CliOptionFlags_Value);
    flagD = cli_register_flag(app, 'd', string_lit("flag-d-multival"), CliOptionFlags_MultiValue);
    argA  = cli_register_arg(app, string_lit("arg-a-req"), CliOptionFlags_Required);
    argB  = cli_register_arg(app, string_lit("arg-b-opt"), CliOptionFlags_None);
  }

  it("succeeds when passing the required options") {
    CliInvocation* invoc = cli_parse(app, 3, (const char*[]){"-a", "Hello World", "ArgVal"});
    parse_check_success(_testCtx, invoc);
    cli_parse_destroy(invoc);
  }

  it("fails when not passing the required options") {
    CliInvocation* invoc = cli_parse(app, 0, null);
    parse_check_fail(
        _testCtx,
        invoc,
        (String[]){
            string_lit("Required option 'flag-a-req' was not provided"),
            string_lit("Required option 'arg-a-req' was not provided"),
        },
        2);
    cli_parse_destroy(invoc);
  }

  teardown() { cli_app_destroy(app); }
}
