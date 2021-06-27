#include "core_ascii.h"

#include "check_spec.h"

spec(ascii) {

  it("can verify validity") {
    check(ascii_is_valid('a'));
    check(!ascii_is_valid(200));
  }

  it("can check if a character is a digit") {
    check(ascii_is_digit('1'));
    check(ascii_is_digit('0'));
    check(!ascii_is_digit('a'));
  }

  it("can check if a character is a hex digit") {
    check(ascii_is_hex_digit('1'));
    check(ascii_is_hex_digit('0'));
    check(ascii_is_hex_digit('a'));
    check(ascii_is_hex_digit('a'));
    check(ascii_is_hex_digit('F'));
    check(ascii_is_hex_digit('f'));
    check(!ascii_is_hex_digit('z'));
  }

  it("can check if a character is a letter") {
    check(ascii_is_letter('a'));
    check(ascii_is_letter('z'));
    check(ascii_is_letter('B'));
    check(!ascii_is_letter('5'));
  }

  it("can check if a character is a lower-case letter") {
    check(ascii_is_lower('a'));
    check(ascii_is_lower('z'));
    check(!ascii_is_lower('B'));
    check(!ascii_is_lower('5'));
  }

  it("can check if a character is a upper-case letter") {
    check(ascii_is_upper('A'));
    check(ascii_is_upper('Z'));
    check(!ascii_is_upper('b'));
    check(!ascii_is_upper('5'));
  }

  it("can check if a character is a control character") {
    check(ascii_is_control('\t'));
    check(ascii_is_control('\a'));
    check(!ascii_is_control('A'));
    check(!ascii_is_control('Z'));
    check(!ascii_is_control('b'));
    check(!ascii_is_control('5'));
  }

  it("can check if a character is a whitespace character") {
    check(ascii_is_whitespace(' '));
    check(ascii_is_whitespace('\n'));
    check(ascii_is_whitespace('\t'));
    check(!ascii_is_whitespace('Z'));
    check(!ascii_is_whitespace('b'));
    check(!ascii_is_whitespace('5'));
  }

  it("can check if a character is a newline character") {
    check(ascii_is_newline('\n'));
    check(ascii_is_newline('\r'));
    check(!ascii_is_newline('Z'));
    check(!ascii_is_newline('b'));
    check(!ascii_is_newline('5'));
  }

  it("can check if a character is printable") {
    check(ascii_is_printable(' '));
    check(ascii_is_printable('Z'));
    check(ascii_is_printable('b'));
    check(ascii_is_printable('5'));
    check(!ascii_is_printable('\n'));
    check(!ascii_is_printable('\r'));
    check(!ascii_is_printable('\a'));
  }

  it("can toggle the casing of a character") {
    check(ascii_toggle_case('a') == 'A');
    check(ascii_toggle_case('A') == 'a');
  }

  it("can convert a character to upper-case") {
    check(ascii_to_upper('a') == 'A');
    check(ascii_to_upper('A') == 'A');
  }

  it("can convert a character to lower-case") {
    check(ascii_to_lower('A') == 'a');
    check(ascii_to_lower('a') == 'a');
  }

  it("can convert a character to an integer") {
    check(ascii_to_integer('0') == 0);
    check(ascii_to_integer('5') == 5);
    check(ascii_to_integer('9') == 9);
    check(ascii_to_integer('a') == 10);
    check(ascii_to_integer('A') == 10);
    check(ascii_to_integer('c') == 12);
    check(ascii_to_integer('C') == 12);
    check(ascii_to_integer('f') == 15);
    check(ascii_to_integer('F') == 15);
    check(sentinel_check(ascii_to_integer(' ')));
    check(sentinel_check(ascii_to_integer('\b')));
  }
}
