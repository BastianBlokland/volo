#include "check_spec.h"
#include "core_dynstring.h"

spec(dynstring) {

  it("can create an empty Dynamic-String") {
    DynString string = dynstring_create_over(mem_stack(128));
    check_eq_int(string.size, 0);
    dynstring_destroy(&string);
  }

  it("can append strings") {
    DynString string = dynstring_create_over(mem_stack(128));

    dynstring_append(&string, string_lit("Hello"));
    check_eq_string(dynstring_view(&string), string_lit("Hello"));

    dynstring_append(&string, string_lit(" "));
    check_eq_string(dynstring_view(&string), string_lit("Hello "));

    dynstring_append(&string, string_lit("World"));
    check_eq_string(dynstring_view(&string), string_lit("Hello World"));

    dynstring_append(&string, string_empty);
    check_eq_string(dynstring_view(&string), string_lit("Hello World"));

    dynstring_destroy(&string);
  }

  it("can append characters") {
    DynString string = dynstring_create_over(mem_stack(128));

    dynstring_append_char(&string, 'H');
    dynstring_append_char(&string, 'e');
    dynstring_append_char(&string, 'l');
    dynstring_append_char(&string, 'l');
    dynstring_append_char(&string, 'o');

    check_eq_string(dynstring_view(&string), string_lit("Hello"));

    dynstring_destroy(&string);
  }

  it("can append sequences of characters") {
    DynString string = dynstring_create_over(mem_stack(128));

    dynstring_append_chars(&string, '*', 3);
    dynstring_append_chars(&string, '-', 5);
    dynstring_append_chars(&string, '*', 3);

    check_eq_string(dynstring_view(&string), string_lit("***-----***"));

    dynstring_destroy(&string);
  }

  it("can insert substrings at specific indices") {
    DynString string = dynstring_create_over(mem_stack(128));

    dynstring_insert(&string, string_lit("World"), 0);
    dynstring_insert(&string, string_lit("Hello"), 0);
    dynstring_insert(&string, string_lit(" "), 5);
    dynstring_insert(&string, string_lit("!"), 11);

    check_eq_string(dynstring_view(&string), string_lit("Hello World!"));

    dynstring_destroy(&string);
  }

  it("can insert character sequences at specific indices") {
    DynString string = dynstring_create_over(mem_stack(128));

    dynstring_insert_chars(&string, '*', 0, 5);
    dynstring_insert_chars(&string, '-', 0, 3);
    dynstring_insert_chars(&string, '-', 8, 3);

    check_eq_string(dynstring_view(&string), string_lit("---*****---"));

    dynstring_destroy(&string);
  }

  it("can push space to the end") {
    DynString string = dynstring_create_over(mem_stack(128));

    mem_set(dynstring_push(&string, 3), '!');

    check_eq_string(dynstring_view(&string), string_lit("!!!"));

    dynstring_destroy(&string);
  }

  it("can replace sub-strings") {
    DynString string = dynstring_create_over(mem_stack(128));
    dynstring_append(&string, string_lit("Hello World"));

    dynstring_replace(&string, string_lit("o"), string_lit("a"));
    check_eq_string(dynstring_view(&string), string_lit("Hella Warld"));

    dynstring_replace(&string, string_lit("o"), string_lit("b"));
    check_eq_string(dynstring_view(&string), string_lit("Hella Warld"));

    dynstring_replace(&string, string_lit("a"), string_lit("###"));
    check_eq_string(dynstring_view(&string), string_lit("Hell### W###rld"));

    dynstring_replace(&string, string_lit("l"), string_lit(""));
    check_eq_string(dynstring_view(&string), string_lit("He### W###rd"));

    dynstring_replace(&string, string_lit("d"), string_lit("!"));
    check_eq_string(dynstring_view(&string), string_lit("He### W###r!"));

    dynstring_replace(&string, string_lit("###"), string_lit("!!"));
    check_eq_string(dynstring_view(&string), string_lit("He!! W!!r!"));

    dynstring_replace(&string, string_lit("r"), string_lit("!"));
    check_eq_string(dynstring_view(&string), string_lit("He!! W!!!!"));

    dynstring_replace(&string, string_lit("!!!"), string_lit("="));
    check_eq_string(dynstring_view(&string), string_lit("He!! W=!"));

    dynstring_destroy(&string);
  }
}
