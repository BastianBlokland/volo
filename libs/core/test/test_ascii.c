#include "core_ascii.h"
#include "core_diag.h"

static void test_ascii_is_valid() {
  diag_assert(ascii_is_valid('a'));
  diag_assert(!ascii_is_valid(200));
}

static void test_ascii_is_digit() {
  diag_assert(ascii_is_digit('1'));
  diag_assert(ascii_is_digit('0'));
  diag_assert(!ascii_is_digit('a'));
}

static void test_ascii_is_hex_digit() {
  diag_assert(ascii_is_hex_digit('1'));
  diag_assert(ascii_is_hex_digit('0'));
  diag_assert(ascii_is_hex_digit('a'));
  diag_assert(ascii_is_hex_digit('a'));
  diag_assert(ascii_is_hex_digit('F'));
  diag_assert(ascii_is_hex_digit('f'));
  diag_assert(!ascii_is_hex_digit('z'));
}

static void test_ascii_is_letter() {
  diag_assert(ascii_is_letter('a'));
  diag_assert(ascii_is_letter('z'));
  diag_assert(ascii_is_letter('B'));
  diag_assert(!ascii_is_letter('5'));
}

static void test_ascii_is_lower() {
  diag_assert(ascii_is_lower('a'));
  diag_assert(ascii_is_lower('z'));
  diag_assert(!ascii_is_lower('B'));
  diag_assert(!ascii_is_lower('5'));
}

static void test_ascii_is_upper() {
  diag_assert(ascii_is_upper('A'));
  diag_assert(ascii_is_upper('Z'));
  diag_assert(!ascii_is_upper('b'));
  diag_assert(!ascii_is_upper('5'));
}

static void test_ascii_is_control() {
  diag_assert(ascii_is_control('\t'));
  diag_assert(ascii_is_control('\a'));
  diag_assert(!ascii_is_control('A'));
  diag_assert(!ascii_is_control('Z'));
  diag_assert(!ascii_is_control('b'));
  diag_assert(!ascii_is_control('5'));
}

static void test_ascii_is_whitespace() {
  diag_assert(ascii_is_whitespace(' '));
  diag_assert(ascii_is_whitespace('\n'));
  diag_assert(ascii_is_whitespace('\t'));
  diag_assert(!ascii_is_whitespace('Z'));
  diag_assert(!ascii_is_whitespace('b'));
  diag_assert(!ascii_is_whitespace('5'));
}

static void test_ascii_is_newline() {
  diag_assert(ascii_is_newline('\n'));
  diag_assert(ascii_is_newline('\r'));
  diag_assert(!ascii_is_newline('Z'));
  diag_assert(!ascii_is_newline('b'));
  diag_assert(!ascii_is_newline('5'));
}

static void test_ascii_is_printable() {
  diag_assert(ascii_is_printable(' '));
  diag_assert(ascii_is_printable('Z'));
  diag_assert(ascii_is_printable('b'));
  diag_assert(ascii_is_printable('5'));
  diag_assert(!ascii_is_printable('\n'));
  diag_assert(!ascii_is_printable('\r'));
  diag_assert(!ascii_is_printable('\a'));
}

static void test_ascii_toggle_case() {
  diag_assert(ascii_toggle_case('a') == 'A');
  diag_assert(ascii_toggle_case('A') == 'a');
}

static void test_ascii_to_upper() {
  diag_assert(ascii_to_upper('a') == 'A');
  diag_assert(ascii_to_upper('A') == 'A');
}

static void test_ascii_to_lower() {
  diag_assert(ascii_to_lower('A') == 'a');
  diag_assert(ascii_to_lower('a') == 'a');
}

static void test_ascii_to_integer() {
  diag_assert(ascii_to_integer('0') == 0);
  diag_assert(ascii_to_integer('5') == 5);
  diag_assert(ascii_to_integer('9') == 9);
  diag_assert(ascii_to_integer('a') == 10);
  diag_assert(ascii_to_integer('A') == 10);
  diag_assert(ascii_to_integer('c') == 12);
  diag_assert(ascii_to_integer('C') == 12);
  diag_assert(ascii_to_integer('f') == 15);
  diag_assert(ascii_to_integer('F') == 15);
  diag_assert(sentinel_check(ascii_to_integer(' ')));
  diag_assert(sentinel_check(ascii_to_integer('\b')));
}

void test_ascii() {
  test_ascii_is_valid();
  test_ascii_is_digit();
  test_ascii_is_hex_digit();
  test_ascii_is_letter();
  test_ascii_is_lower();
  test_ascii_is_upper();
  test_ascii_is_control();
  test_ascii_is_whitespace();
  test_ascii_is_newline();
  test_ascii_is_printable();
  test_ascii_toggle_case();
  test_ascii_to_upper();
  test_ascii_to_lower();
  test_ascii_to_integer();
}
