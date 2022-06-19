#include "check_spec.h"
#include "core_alloc.h"
#include "core_sentinel.h"
#include "core_sort.h"
#include "core_string.h"

spec(string) {

  it("can hash a string") {
    check_eq_int(string_hash_lit("Hello World"), string_hash_lit("Hello World"));
    check_eq_int(string_hash_lit(""), string_hash_lit(""));
  }

  it("can create a string from a null-terminated character array") {
    check_eq_string(string_from_null_term("Hello World"), string_lit("Hello World"));
    check_eq_string(string_from_null_term("Hello\0World"), string_lit("Hello"));
    check_eq_string(string_from_null_term("\0World"), string_empty);
  }

  it("can retrieve the length of a string") {
    check_eq_int(string_empty.size, 0);
    check_eq_int(string_lit("").size, 0);
    check_eq_int(string_lit("H").size, 1);
    check_eq_int(string_lit("Hello World").size, 11);
  }

  it("can check if a string is empty") {
    check(string_is_empty(string_empty));
    check(string_is_empty(string_lit("")));
    check(!string_is_empty(string_lit("Hello World")));
  }

  it("can retrieve the last character") {
    check_eq_int(*string_last(string_lit("Hello World")), 'd');
    check_eq_int(*string_last(string_lit(" ")), ' ');
  }

  it("can combine strings") {
    check_eq_string(string_combine(g_alloc_scratch), string_empty);
    check_eq_string(string_combine(g_alloc_scratch, string_lit("")), string_empty);
    check_eq_string(string_combine(g_alloc_scratch, string_lit("Hello")), string_lit("Hello"));
    check_eq_string(
        string_combine(g_alloc_scratch, string_lit("Hello"), string_lit("World")),
        string_lit("HelloWorld"));
    check_eq_string(
        string_combine(g_alloc_scratch, string_lit("Hello"), string_lit(" "), string_lit("World")),
        string_lit("Hello World"));
    check_eq_string(
        string_combine(
            g_alloc_scratch,
            string_lit(""),
            string_lit("Hello"),
            string_lit(" "),
            string_lit(""),
            string_lit("World"),
            string_lit("")),
        string_lit("Hello World"));
  }

  it("can compare strings") {
    check_eq_int(string_cmp(string_lit("a"), string_lit("a")), 0);
    check_eq_int(string_cmp(string_lit("a"), string_lit("b")), -1);
    check_eq_int(string_cmp(string_lit("b"), string_lit("a")), 1);
    check_eq_int(string_cmp(string_lit("April"), string_lit("March")), -1);
    check_eq_int(string_cmp(string_lit("March"), string_lit("December")), 1);
  }

  it("can check if strings are equal") {
    check_eq_string(string_empty, string_empty);
    check_eq_string(string_lit("Hello World"), string_lit("Hello World"));

    check(!string_eq(string_empty, string_lit("H")));
    check(!string_eq(string_lit("Hello Worl"), string_lit("Hello World")));
    check(!string_eq(string_lit("ello World"), string_lit("Hello World")));
  }

  it("can check if a string starts with a sub-string") {
    check(string_starts_with(string_empty, string_empty));
    check(string_starts_with(string_lit("Hello World"), string_lit("Hello")));
    check(string_starts_with(string_lit("Hello"), string_lit("Hello")));
    check(!string_starts_with(string_lit("Hell"), string_lit("Hello")));
    check(!string_starts_with(string_lit("Hello World"), string_lit("Stuff")));
  }

  it("can check if a string ends with a sub-string") {
    check(string_ends_with(string_empty, string_empty));
    check(string_ends_with(string_lit("Hello World"), string_lit("World")));
    check(string_ends_with(string_lit("Hello"), string_lit("Hello")));
    check(!string_ends_with(string_lit("Hell"), string_lit("ello")));
    check(!string_ends_with(string_lit("Hello World"), string_lit("Stuff")));
  }

  it("can slice a string") {
    check_eq_string(string_slice(string_lit("Hello World"), 0, 5), string_lit("Hello"));
    check_eq_string(string_slice(string_lit("Hello World"), 6, 5), string_lit("World"));
  }

  it("can consume characters from a string") {
    check_eq_string(string_consume(string_lit("Hello World"), 5), string_lit(" World"));
    check_eq_string(string_consume(string_lit(" "), 1), string_empty);
    check_eq_string(string_consume(string_empty, 0), string_empty);
    check_eq_string(string_consume(string_lit("Hello"), 0), string_lit("Hello"));
  }

  it("can find the first occurrence of a sub-string") {
    check_eq_int(string_find_first(string_lit("Hello World"), string_lit("Hello")), 0);
    check_eq_int(string_find_first(string_lit("Hello World"), string_lit("Hello World")), 0);
    check_eq_int(string_find_first(string_lit("Hello World"), string_lit("World")), 6);
    check_eq_int(string_find_first(string_lit("Hello World"), string_lit("lo")), 3);
    check_eq_int(string_find_first(string_lit(" Hi Hi Hi "), string_lit("Hi")), 1);
    check_eq_int(string_find_first(string_lit("Hello World"), string_lit("d")), 10);
    check_eq_int(string_find_first(string_lit("Hello World"), string_lit("ld")), 9);
    check_eq_int(string_find_first(string_lit("Hello World"), string_lit("H")), 0);
    check_eq_int(string_find_first(string_lit("Hello World"), string_lit("H")), 0);
    check_eq_int(string_find_first(string_lit("Hello World"), string_lit("He")), 0);
    check_eq_int(string_find_first(string_lit("Hello World"), string_lit("q")), sentinel_usize);
    check_eq_int(
        string_find_first(string_lit("Hello World"), string_lit("Hello World!")), sentinel_usize);
  }

  it("can find the first occurrence of any of the specified characters") {
    check_eq_int(string_find_first_any(string_empty, string_lit(" ")), sentinel_usize);
    check_eq_int(string_find_first_any(string_empty, string_lit("\0")), sentinel_usize);
    check_eq_int(string_find_first_any(string_lit("\0"), string_lit("\n\r\0")), 0);
    check_eq_int(string_find_first_any(string_lit("Hello World"), string_lit(" ")), 5);
    check_eq_int(string_find_first_any(string_lit("Hello World"), string_lit("or")), 4);
    check_eq_int(
        string_find_first_any(string_lit("Hello World"), string_lit("zqx")), sentinel_usize);
  }

  it("can find the last occurrence of a sub-string") {
    check_eq_int(string_find_last(string_lit("Hello World"), string_lit("Hello")), 0);
    check_eq_int(string_find_last(string_lit("Hello World"), string_lit("Hello World")), 0);
    check_eq_int(string_find_last(string_lit("Hello World"), string_lit("World")), 6);
    check_eq_int(string_find_last(string_lit("Hello World"), string_lit("lo")), 3);
    check_eq_int(string_find_last(string_lit(" Hi Hi Hi "), string_lit("Hi")), 7);
    check_eq_int(string_find_last(string_lit("Hello World"), string_lit("d")), 10);
    check_eq_int(string_find_last(string_lit("Hello World"), string_lit("ld")), 9);
    check_eq_int(string_find_last(string_lit("Hello World"), string_lit("H")), 0);
    check_eq_int(string_find_last(string_lit("Hello World"), string_lit("H")), 0);
    check_eq_int(string_find_last(string_lit("Hello World"), string_lit("He")), 0);
    check_eq_int(string_find_last(string_lit("Hello World"), string_lit("q")), sentinel_usize);
    check_eq_int(
        string_find_last(string_lit("Hello World"), string_lit("Hello World!")), sentinel_usize);
  }

  it("can find the last occurrence of any of the specified characters") {
    check_eq_int(string_find_last_any(string_empty, string_lit(" ")), sentinel_usize);
    check_eq_int(string_find_last_any(string_empty, string_lit("\0")), sentinel_usize);
    check_eq_int(string_find_last_any(string_lit("\0"), string_lit("\n\r\0")), 0);
    check_eq_int(string_find_last_any(string_lit("Hello World"), string_lit(" ")), 5);
    check_eq_int(string_find_last_any(string_lit("Hello World"), string_lit("or")), 8);
    check_eq_int(string_find_last_any(string_lit("Hello World"), string_lit("d")), 10);
    check_eq_int(string_find_last_any(string_lit("Hello World"), string_lit("hH")), 0);
    check_eq_int(
        string_find_last_any(string_lit("Hello World"), string_lit("zqx")), sentinel_usize);
  }

  it("can be added to a Dynamic-Array") {
    Allocator* alloc = alloc_bump_create_stack(1024);
    DynArray   array = dynarray_create_t(alloc, String, 4);

    for (i32 i = 0; i != 4; ++i) {
      *dynarray_push_t(&array, String) =
          string_dup(alloc, fmt_write_scratch("Hello {}", fmt_int(i)));
    }

    check_eq_string(*dynarray_at_t(&array, 0, String), string_lit("Hello 0"));
    check_eq_string(*dynarray_at_t(&array, 1, String), string_lit("Hello 1"));
    check_eq_string(*dynarray_at_t(&array, 2, String), string_lit("Hello 2"));
    check_eq_string(*dynarray_at_t(&array, 3, String), string_lit("Hello 3"));

    dynarray_for_t(&array, String, str) { string_free(alloc, *str); }
    dynarray_destroy(&array);
  }

  it("can match glob patterns") {
    const StringMatchFlags f = StringMatchFlags_None;
    check(string_match_glob(string_lit("hello"), string_lit("*"), f));
    check(string_match_glob(string_lit("world"), string_lit("*world"), f));
    check(string_match_glob(string_lit(" world"), string_lit("*world"), f));
    check(string_match_glob(string_lit("helloworld"), string_lit("*world"), f));
    check(string_match_glob(string_lit("helloworld"), string_lit("hello*world"), f));
    check(string_match_glob(string_lit("hello world"), string_lit("hello*world"), f));
    check(string_match_glob(string_lit("hellostuffworld"), string_lit("hello*world"), f));
    check(string_match_glob(
        string_lit("hellostuffworldsomemore"), string_lit("hello*world*more"), f));
    check(string_match_glob(string_lit("hellostuffworldmore"), string_lit("hello*world*more"), f));
    check(string_match_glob(string_lit("world"), string_lit("*world*"), f));
    check(string_match_glob(string_lit("helloworldmore"), string_lit("*world*"), f));
    check(string_match_glob(string_lit("world"), string_lit("**"), f));
    check(string_match_glob(string_empty, string_lit("*"), f));
    check(string_match_glob(string_empty, string_empty, f));
    check(string_match_glob(string_lit("a"), string_lit("?"), f));
    check(string_match_glob(string_lit(" "), string_lit("?"), f));
    check(string_match_glob(string_lit("hello world"), string_lit("h??lo?w?rld"), f));
    check(string_match_glob(string_lit("hello"), string_lit("hello"), f));

    check(!string_match_glob(string_lit("hello"), string_lit("*world"), f));
    check(!string_match_glob(string_lit("worldhello"), string_lit("*world"), f));
    check(!string_match_glob(string_empty, string_lit("hello"), f));
    check(!string_match_glob(string_lit("world"), string_lit("hello"), f));
    check(!string_match_glob(string_lit("helloworld"), string_lit("hello"), f));
    check(!string_match_glob(string_lit("worldhello"), string_lit("hello"), f));
    check(!string_match_glob(string_lit("hello"), string_empty, f));
    check(!string_match_glob(string_lit("hellostuffworl"), string_lit("hello*world"), f));
    check(!string_match_glob(string_lit("hellstuffworl"), string_lit("hello*world"), f));
    check(!string_match_glob(string_lit("hellostuffworld"), string_lit("hello*world*more"), f));
    check(!string_match_glob(string_empty, string_lit("?"), f));
    check(!string_match_glob(string_lit("ello world"), string_lit("h??lo?w?rld?"), f));
    check(!string_match_glob(string_lit("helloworld"), string_lit("h??lo?w?rld?"), f));
    check(!string_match_glob(string_lit("hello world"), string_lit("h??lo?w?rld?"), f));

    check(string_match_glob(string_lit("HeLlO"), string_lit("hello"), StringMatchFlags_IgnoreCase));
    check(
        !string_match_glob(string_lit("HeLlOZ"), string_lit("hello"), StringMatchFlags_IgnoreCase));
  }

  it("can be trimmed") {
    const String trimChars = string_lit("-.");
    check_eq_string(string_trim(string_lit("-.hello.-"), trimChars), string_lit("hello"));
    check_eq_string(string_trim(string_lit("-.h.e-l.l-o.-"), trimChars), string_lit("h.e-l.l-o"));
    check_eq_string(string_trim(string_lit("----"), trimChars), string_empty);
    check_eq_string(string_trim(string_empty, trimChars), string_empty);
  }

  it("can be trimmed of whitespace") {
    check_eq_string(string_trim_whitespace(string_lit("  hello ")), string_lit("hello"));
    check_eq_string(string_trim_whitespace(string_lit("\r\n\thello\t")), string_lit("hello"));
    check_eq_string(string_trim_whitespace(string_lit("\vhello")), string_lit("hello"));
    check_eq_string(string_trim_whitespace(string_lit("hello world")), string_lit("hello world"));
    check_eq_string(string_trim_whitespace(string_lit(" \t\n")), string_empty);
    check_eq_string(string_trim_whitespace(string_empty), string_empty);
  }

  it("can be sorted") {
    Allocator* alloc = alloc_bump_create_stack(1024);
    DynArray   array = dynarray_create_t(alloc, String, 4);

    *dynarray_push_t(&array, String) = string_dup(alloc, string_lit("May"));
    *dynarray_push_t(&array, String) = string_dup(alloc, string_lit("November"));
    *dynarray_push_t(&array, String) = string_dup(alloc, string_lit("April"));

    dynarray_sort(&array, compare_string);

    check_eq_string(*dynarray_at_t(&array, 0, String), string_lit("April"));
    check_eq_string(*dynarray_at_t(&array, 1, String), string_lit("May"));
    check_eq_string(*dynarray_at_t(&array, 2, String), string_lit("November"));

    dynarray_for_t(&array, String, str) { string_free(alloc, *str); }
    dynarray_destroy(&array);
  }
}
