#include "check_spec.h"
#include "core_array.h"
#include "script_format.h"

spec(format) {
  Mem       buffer = mem_stack(4096);
  DynString bufferStr;

  setup() { bufferStr = dynstring_create_over(buffer); }

  it("normalizes whitespace in lines") {
    static const struct {
      String input, expect;
    } g_testData[] = {
        {string_static("\n"), string_static("\n")},
        {string_static(" 42\n"), string_static("42\n")},
        {string_static("1;2;3;4\n"), string_static("1; 2; 3; 4\n")},
        {string_static("1;2;3;4;\n"), string_static("1; 2; 3; 4;\n")},
        {string_static(" \t 42\n"), string_static("42\n")},
        {string_static(" 42  \t \n"), string_static("42\n")},
        {string_static("1+2\n"), string_static("1 + 2\n")},
        {string_static("1/2\n"), string_static("1 / 2\n")},
        {string_static("1?2:3\n"), string_static("1 ? 2 : 3\n")},
        {string_static("1>2?1+2:3+4\n"), string_static("1 > 2 ? 1 + 2 : 3 + 4\n")},
        {string_static("var a;a+=42\n"), string_static("var a; a += 42\n")},
        {string_static("true&&2*4\n"), string_static("true && 2 * 4\n")},
        {string_static(" return \n"), string_static("return\n")},
        {string_static(" return 42 \n"), string_static("return 42\n")},
        {string_static("{return}\n"), string_static("{ return }\n")},
        {string_static("{return 42}\n"), string_static("{ return 42 }\n")},
        {string_static("{return 42;}\n"), string_static("{ return 42; }\n")},
        {string_static("var test=42\n"), string_static("var test = 42\n")},
        {string_static("if( true ){\n"), string_static("if (true) {\n")},
        {string_static("( 1 + ( 2 ) )\n"), string_static("(1 + (2))\n")},
        {string_static("(($hello))\n"), string_static("(($hello))\n")},
        {string_static("test ( 42 )\n"), string_static("test(42)\n")},
        {string_static("test ( 42 , 1337 )\n"), string_static("test(42, 1337)\n")},
        {string_static("test ()\n"), string_static("test()\n")},
        {string_static("$test=42\n"), string_static("$test = 42\n")},
        {string_static("42 ; \n"), string_static("42;\n")},
        {string_static("-42\n"), string_static("-42\n")},
        {string_static("---42\n"), string_static("---42\n")},
        {string_static("!42\n"), string_static("!42\n")},
        {string_static("!true\n"), string_static("!true\n")},
        {string_static("!-42\n"), string_static("!-42\n")},
        {string_static("-(42+1)\n"), string_static("-(42 + 1)\n")},
        {string_static("-test()\n"), string_static("-test()\n")},
        {string_static("test(42) - test(1337)\n"), string_static("test(42) - test(1337)\n")},
        {string_static("1 - 2\n"), string_static("1 - 2\n")},
        {
            string_static("for(var i=0;i!=100;i+=1){\n"),
            string_static("for (var i = 0; i != 100; i += 1) {\n"),
        },
        {string_static("for(;;) {}\n"), string_static("for (;;) { }\n")},
        {string_static("for(;;) {break}\n"), string_static("for (;;) { break }\n")},
        {string_static("for(;true;) {}\n"), string_static("for (; true;) { }\n")},
        {
            string_static("while(i<42){\n"),
            string_static("while (i < 42) {\n"),
        },
        {
            string_static("if(false) {2} else if(true) {3}\n"),
            string_static("if (false) { 2 } else if (true) { 3 }\n"),
        },
        {
            string_static("var sqrOf42={var i=42;i*i}\n"),
            string_static("var sqrOf42 = { var i = 42; i * i }\n"),
        },
    };

    for (u32 i = 0; i != array_elems(g_testData); ++i) {
      dynstring_clear(&bufferStr);
      script_format(&bufferStr, g_testData[i].input);
      check_eq_string(dynstring_view(&bufferStr), g_testData[i].expect);
    }
  }

  teardown() { dynstring_destroy(&bufferStr); }
}
