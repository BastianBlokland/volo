#include "check/spec.h"
#include "cli/app.h"
#include "cli/parse.h"
#include "cli/validate.h"
#include "core/alloc.h"
#include "core/math.h"

static void parse_check_success(CheckTestContext* _testCtx, CliInvocation* invoc) {
  check_eq_int(cli_parse_result(invoc), CliParseResult_Success);

  for (usize i = 0; i != cli_parse_errors(invoc).count; ++i) {
    check_eq_string(cli_parse_errors(invoc).values[i], string_empty);
  }
}

static void parse_check_fail(
    CheckTestContext* _testCtx, CliInvocation* invoc, const String* errHead, const usize errCount) {
  check_eq_int(cli_parse_result(invoc), CliParseResult_Fail);

  check_eq_int(cli_parse_errors(invoc).count, errCount);
  const usize errsToCheck = math_min(errCount, cli_parse_errors(invoc).count);
  for (usize i = 0; i != errsToCheck; ++i) {
    check_eq_string(cli_parse_errors(invoc).values[i], errHead[i]);
  }
}

static void parse_check_values(
    CheckTestContext* _testCtx,
    CliInvocation*    invoc,
    const CliId       id,
    const String*     valHead,
    const usize       valCount) {

  check_eq_int(cli_parse_values(invoc, id).count, valCount);
  const usize valsToCheck = math_min(valCount, cli_parse_values(invoc, id).count);
  for (usize i = 0; i != valsToCheck; ++i) {
    check_eq_string(cli_parse_values(invoc, id).values[i], valHead[i]);
  }
}

spec(parse) {

  CliApp* app = null;
  CliId   flagA, flagB, flagC, flagD, flagE, flagF, flagG, flagH, argA, argB;

  setup() {
    app   = cli_app_create(g_allocHeap);
    flagA = cli_register_flag(app, 'a', string_lit("flag-a-req"), CliOptionFlags_Required);
    flagB = cli_register_flag(app, 'b', string_lit("flag-b-opt"), CliOptionFlags_None);
    flagC = cli_register_flag(app, 'c', string_lit("flag-c-opt"), CliOptionFlags_None);
    flagD = cli_register_flag(app, 'd', string_lit("flag-d-val"), CliOptionFlags_Value);
    flagE = cli_register_flag(app, '\0', string_lit("flag-e-multival"), CliOptionFlags_MultiValue);
    flagF = cli_register_flag(app, '\0', string_lit("flag-f"), CliOptionFlags_None);
    flagG = cli_register_flag(app, '\0', string_lit("flag-g"), CliOptionFlags_None);
    flagH = cli_register_flag(app, '\0', string_lit("flag-h"), CliOptionFlags_Exclusive);
    argA  = cli_register_arg(app, string_lit("arg-a-req"), CliOptionFlags_Required);
    argB  = cli_register_arg(app, string_lit("arg-b-opt"), CliOptionFlags_MultiValue);

    cli_register_validator(app, flagD, cli_validate_i64);
    cli_register_validator(app, argB, cli_validate_i64);

    cli_register_exclusion(app, flagA, flagG);
    cli_register_exclusion(app, flagD, flagE);
    cli_register_exclusion(app, flagE, flagF);
    cli_register_exclusion(app, argB, flagE);
    (void)flagH;
  }

  it("succeeds when passing the required options") {
    CliInvocation* invoc =
        cli_parse_lit(app, string_lit("-a"), string_lit("Hello"), string_lit("ArgVal"));
    parse_check_success(_testCtx, invoc);
    cli_parse_destroy(invoc);
  }

  it("fails when omitting required options") {
    CliInvocation* invoc = cli_parse(app, null, 0);
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

  it("supports both short and long forms for flags") {
    CliInvocation* invoc = cli_parse_lit(
        app,
        string_lit("--flag-a-req"),
        string_lit("Hello"),
        string_lit("-d"),
        string_lit("42"),
        string_lit("-c"),
        string_lit("ArgVal"));
    parse_check_success(_testCtx, invoc);
    check(cli_parse_provided(invoc, flagA));
    check(cli_parse_provided(invoc, flagC));
    check(cli_parse_provided(invoc, flagD));
    cli_parse_destroy(invoc);
  }

  it("supports passing multiple short flags in a single block") {
    CliInvocation* invoc = cli_parse_lit(
        app, string_lit("-bc"), string_lit("-a"), string_lit("Hello"), string_lit("ArgVal"));
    parse_check_success(_testCtx, invoc);
    check(cli_parse_provided(invoc, flagB));
    check(cli_parse_provided(invoc, flagC));
    cli_parse_destroy(invoc);
  }

  it("supports long form flags with a value") {
    CliInvocation* invoc = cli_parse_lit(
        app, string_lit("--flag-a-req"), string_lit("Hello World"), string_lit("ArgVal"));
    parse_check_success(_testCtx, invoc);
    parse_check_values(_testCtx, invoc, flagA, (String[]){string_lit("Hello World")}, 1);
    cli_parse_destroy(invoc);
  }

  it("supports short form flags with a value") {
    CliInvocation* invoc =
        cli_parse_lit(app, string_lit("-a"), string_lit("Hello World"), string_lit("ArgVal"));
    parse_check_success(_testCtx, invoc);
    parse_check_values(_testCtx, invoc, flagA, (String[]){string_lit("Hello World")}, 1);
    cli_parse_destroy(invoc);
  }

  it("supports retrieving argument values") {
    CliInvocation* invoc = cli_parse_lit(
        app, string_lit("Hello World"), string_lit("-a"), string_lit("Hello"), string_lit("42"));
    parse_check_success(_testCtx, invoc);
    parse_check_values(_testCtx, invoc, argA, (String[]){string_lit("Hello World")}, 1);
    parse_check_values(_testCtx, invoc, argB, (String[]){string_lit("42")}, 1);
    cli_parse_destroy(invoc);
  }

  it("supports value flags with multiple values as separate strings") {
    CliInvocation* invoc = cli_parse_lit(
        app,
        string_lit("--flag-e-multival"),
        string_lit("Hello"),
        string_lit("Beautiful"),
        string_lit("World"),
        string_lit("-a"),
        string_lit("Hello"),
        string_lit("ArgVal"));
    parse_check_values(
        _testCtx,
        invoc,
        flagE,
        (String[]){string_lit("Hello"), string_lit("Beautiful"), string_lit("World")},
        3);
    cli_parse_destroy(invoc);
  }

  it("supports value flags with multiple values as a single string") {
    CliInvocation* invoc = cli_parse_lit(
        app,
        string_lit("--flag-e-multival"),
        string_lit("Hello,Beautiful,World"),
        string_lit("-a"),
        string_lit("Hello"),
        string_lit("ArgVal"));
    parse_check_values(
        _testCtx,
        invoc,
        flagE,
        (String[]){string_lit("Hello"), string_lit("Beautiful"), string_lit("World")},
        3);
    cli_parse_destroy(invoc);
  }

  it("supports value flags with multiple values as a mix of single and multiple strings") {
    CliInvocation* invoc = cli_parse_lit(
        app,
        string_lit("--flag-e-multival"),
        string_lit("A,,,B"),
        string_lit("C,,"),
        string_lit("D,E"),
        string_lit("-a"),
        string_lit("Hello"),
        string_lit("ArgVal"));
    parse_check_values(
        _testCtx,
        invoc,
        flagE,
        (String[]){
            string_lit("A"), string_lit("B"), string_lit("C"), string_lit("D"), string_lit("E")},
        5);
    cli_parse_destroy(invoc);
  }

  it("supports arguments with multiple values as separate strings") {
    CliInvocation* invoc = cli_parse_lit(
        app,
        string_lit("-a"),
        string_lit("Hello"),
        string_lit("ArgVal"),
        string_lit("Hello"),
        string_lit("Beautiful"),
        string_lit("World"));
    parse_check_values(
        _testCtx,
        invoc,
        argB,
        (String[]){string_lit("Hello"), string_lit("Beautiful"), string_lit("World")},
        3);
    cli_parse_destroy(invoc);
  }

  it("supports arguments with multiple values as a single string") {
    CliInvocation* invoc = cli_parse_lit(
        app,
        string_lit("-a"),
        string_lit("Hello"),
        string_lit("ArgVal"),
        string_lit("Hello,Beautiful,World"));
    parse_check_values(
        _testCtx,
        invoc,
        argB,
        (String[]){string_lit("Hello"), string_lit("Beautiful"), string_lit("World")},
        3);
    cli_parse_destroy(invoc);
  }

  it("supports arguments with multiple values as a mix of single and multiple strings") {
    CliInvocation* invoc = cli_parse_lit(
        app,
        string_lit("-a"),
        string_lit("Hello"),
        string_lit("ArgVal"),
        string_lit(",A,B,"),
        string_lit("C"),
        string_lit("D,E"));
    parse_check_values(
        _testCtx,
        invoc,
        argB,
        (String[]){
            string_lit("A"), string_lit("B"), string_lit("C"), string_lit("D"), string_lit("E")},
        5);
    cli_parse_destroy(invoc);
  }

  it("supports single dash for terminating a list of values") {
    CliInvocation* invoc = cli_parse_lit(
        app,
        string_lit("-a"),
        string_lit("Hello"),
        string_lit("ArgVal"),
        string_lit("--flag-e-multival"),
        string_lit("Some"),
        string_lit("Values"),
        string_lit("-"),
        string_lit("Hello"),
        string_lit("World"));
    parse_check_values(
        _testCtx, invoc, flagE, (String[]){string_lit("Some"), string_lit("Values")}, 2);
    parse_check_values(
        _testCtx, invoc, argB, (String[]){string_lit("Hello"), string_lit("World")}, 2);
    cli_parse_destroy(invoc);
  }

  it("supports double dash to stop accepting flags") {
    CliInvocation* invoc = cli_parse_lit(
        app,
        string_lit("-a"),
        string_lit("Hello"),
        string_lit("--"),
        string_lit("-b"),
        string_lit("--some-value"),
        string_lit("-"),
        string_lit("--"));
    parse_check_values(_testCtx, invoc, argA, (String[]){string_lit("-b")}, 1);
    parse_check_values(
        _testCtx,
        invoc,
        argB,
        (String[]){string_lit("--some-value"), string_lit("-"), string_lit("--")},
        3);
    cli_parse_destroy(invoc);
  }

  it("ignores empty values") {
    CliInvocation* invoc = cli_parse_lit(
        app,
        string_lit(""),
        string_lit("-a"),
        string_lit(""),
        string_lit("Hello"),
        string_lit(""),
        string_lit("ArgVal1"),
        string_lit(""),
        string_lit("1337"),
        string_lit(""),
        string_lit(""),
        string_lit("42"),
        string_lit(""));
    parse_check_success(_testCtx, invoc);
    parse_check_values(_testCtx, invoc, flagA, (String[]){string_lit("Hello")}, 1);
    parse_check_values(_testCtx, invoc, argA, (String[]){string_lit("ArgVal1")}, 1);
    parse_check_values(_testCtx, invoc, argB, (String[]){string_lit("1337"), string_lit("42")}, 2);
    cli_parse_destroy(invoc);
  }

  it("fails when passing the same flag twice in short form") {
    CliInvocation* invoc = cli_parse_lit(
        app,
        string_lit("-a"),
        string_lit("Hello"),
        string_lit("-a"),
        string_lit("World"),
        string_lit("42"));
    parse_check_fail(_testCtx, invoc, (String[]){string_lit("Duplicate flag 'a'")}, 1);
    cli_parse_destroy(invoc);
  }

  it("fails when passing the same flag twice in long form") {
    CliInvocation* invoc = cli_parse_lit(
        app,
        string_lit("--flag-a-req"),
        string_lit("Hello"),
        string_lit("--flag-a-req"),
        string_lit("World"),
        string_lit("42"));
    parse_check_fail(_testCtx, invoc, (String[]){string_lit("Duplicate flag 'flag-a-req'")}, 1);
    cli_parse_destroy(invoc);
  }

  it("fails when passing the same flag twice in a flag block") {
    CliInvocation* invoc = cli_parse_lit(
        app, string_lit("-bbc"), string_lit("-a"), string_lit("Hello"), string_lit("ArgVal"));
    parse_check_fail(_testCtx, invoc, (String[]){string_lit("Duplicate flag 'b'")}, 1);
    cli_parse_destroy(invoc);
  }

  it("fails when trying to pass a flag with a value in a flag block") {
    CliInvocation* invoc =
        cli_parse_lit(app, string_lit("-ba"), string_lit("Hello"), string_lit("42"));
    parse_check_fail(
        _testCtx,
        invoc,
        (String[]){
            string_lit("Flag 'a' takes a value"),
            string_lit("Required option 'flag-a-req' was not provided")},
        2);
    cli_parse_destroy(invoc);
  }

  it("fails when omitting the value for a value flag") {
    CliInvocation* invoc = cli_parse_lit(
        app, string_lit("-a"), string_lit("Hello"), string_lit("ArgVal"), string_lit("-d"));
    parse_check_fail(
        _testCtx, invoc, (String[]){string_lit("Value missing for option 'flag-d-val'")}, 1);
    cli_parse_destroy(invoc);
  }

  it("fails when passing the same flag in both short and long form") {
    CliInvocation* invoc = cli_parse_lit(
        app,
        string_lit("-a"),
        string_lit("Hello"),
        string_lit("--flag-a-req"),
        string_lit("World"),
        string_lit("42"));
    parse_check_fail(_testCtx, invoc, (String[]){string_lit("Duplicate flag 'flag-a-req'")}, 1);
    cli_parse_destroy(invoc);
  }

  it("fails when passing an unknown flag in short form") {
    CliInvocation* invoc = cli_parse_lit(
        app, string_lit("-a"), string_lit("Hello"), string_lit("-g"), string_lit("ArgVal"));
    parse_check_fail(_testCtx, invoc, (String[]){string_lit("Unknown flag 'g'")}, 1);
    cli_parse_destroy(invoc);
  }

  it("fails when passing an unknown flag in long form") {
    CliInvocation* invoc = cli_parse_lit(
        app,
        string_lit("-a"),
        string_lit("Hello"),
        string_lit("--some-flag"),
        string_lit("ArgVal"));
    parse_check_fail(_testCtx, invoc, (String[]){string_lit("Unknown flag 'some-flag'")}, 1);
    cli_parse_destroy(invoc);
  }

  it("fails when passing an unknown flag in a flag block") {
    CliInvocation* invoc = cli_parse_lit(
        app, string_lit("-bgc"), string_lit("-a"), string_lit("Hello"), string_lit("ArgVal"));
    parse_check_fail(_testCtx, invoc, (String[]){string_lit("Unknown flag 'g'")}, 1);
    cli_parse_destroy(invoc);
  }

  it("fails when providing more arguments then expected") {
    CliInvocation* invoc = cli_parse_lit(
        app,
        string_lit("-a"),
        string_lit("Hello"),
        string_lit("Arg1Val"),
        string_lit("1337"),
        string_lit("-"),
        string_lit("AnotherArg"));
    parse_check_fail(_testCtx, invoc, (String[]){string_lit("Invalid input 'AnotherArg'")}, 1);
    cli_parse_destroy(invoc);
  }

  it("fails when providing a value that is incompatible with the validator") {
    CliInvocation* invoc = cli_parse_lit(
        app,
        string_lit("-a"),
        string_lit("Hello"),
        string_lit("ArgVal"),
        string_lit("1"),
        string_lit("Hello"),
        string_lit("World"));
    parse_check_fail(
        _testCtx,
        invoc,
        (String[]){
            string_lit("Invalid input 'Hello' for option 'arg-b-opt'"),
            string_lit("Invalid input 'World' for option 'arg-b-opt'")},
        2);
    cli_parse_destroy(invoc);
  }

  it("fails when combining an exclusive option with another option") {
    CliInvocation* invoc = cli_parse_lit(app, string_lit("--flag-b-opt"), string_lit("--flag-h"));
    parse_check_fail(
        _testCtx,
        invoc,
        (String[]){
            string_lit("Exclusive option 'flag-h' cannot be used together with another option")},
        1);
    cli_parse_destroy(invoc);
  }

  it("fails when violating an exclusion") {
    CliInvocation* invoc = cli_parse_lit(
        app,
        string_lit("-d"),
        string_lit("42"),
        string_lit("--flag-e-multival"),
        string_lit("B"),
        string_lit("-a"),
        string_lit("Hello"),
        string_lit("ArgVal"));
    parse_check_fail(
        _testCtx,
        invoc,
        (String[]){
            string_lit("Options 'flag-d-val' and 'flag-e-multival' cannot be used together")},
        1);
    cli_parse_destroy(invoc);
  }

  it("succeeds when providing an exclusive option instead of a required option") {
    CliInvocation* invoc = cli_parse_lit(app, string_lit("--flag-h"));
    parse_check_success(_testCtx, invoc);
    parse_check_values(_testCtx, invoc, argA, null, 0);
    cli_parse_destroy(invoc);
  }

  it("succeeds when providing an alternative instead of a required option") {
    CliInvocation* invoc = cli_parse_lit(app, string_lit("ArgVal"), string_lit("--flag-g"));
    parse_check_success(_testCtx, invoc);
    parse_check_values(_testCtx, invoc, argA, (String[]){string_lit("ArgVal")}, 1);
    cli_parse_destroy(invoc);
  }

  teardown() { cli_app_destroy(app); }
}
