#include "core_array.h"
#include "core_format.h"
#include "core_path.h"
#include "core_rng.h"

#include "check_spec.h"

spec(path) {

  it("can check if a path is absolute") {
    check(path_is_absolute(string_lit("/")));
    check(path_is_absolute(string_lit("c:/")));
    check(path_is_absolute(string_lit("C:/")));
    check(path_is_absolute(string_lit("C:\\")));

    check(!path_is_absolute(string_lit("Hello")));
    check(!path_is_absolute(string_lit("./")));
    check(!path_is_absolute(string_lit("../")));
    check(!path_is_absolute(string_lit("\\")));
  }

  it("can check if a path is a root") {
    check(path_is_root(string_lit("/")));
    check(path_is_root(string_lit("c:/")));
    check(path_is_root(string_lit("C:/")));
    check(path_is_root(string_lit("C:\\")));

    check(!path_is_root(string_lit("Hello")));
    check(!path_is_root(string_lit("/Hello")));
    check(!path_is_root(string_lit("c:/Hello")));
  }

  it("can retrieve the file-name of a path") {
    check_eq_string(path_filename(string_lit("note.txt")), string_lit("note.txt"));
    check_eq_string(path_filename(string_lit("/stuff/note.txt")), string_lit("note.txt"));
    check_eq_string(path_filename(string_lit("c:/stuff/note.txt")), string_lit("note.txt"));
    check_eq_string(path_filename(string_lit("c:/stuff/")), string_empty);
    check_eq_string(path_filename(string_lit("/")), string_empty);
  }

  it("can retrieve the extension of a path") {
    check_eq_string(path_extension(string_lit("note.txt")), string_lit("txt"));
    check_eq_string(path_extension(string_lit("note.txt.back")), string_lit("back"));
    check_eq_string(path_extension(string_lit("c:/.stuff/note.txt")), string_lit("txt"));
    check_eq_string(path_extension(string_lit("c:/.stuff/note")), string_empty);
    check_eq_string(path_extension(string_lit("c:/.stuff/note.")), string_empty);
    check_eq_string(path_extension(string_lit("c:/.stuff/.")), string_empty);
    check_eq_string(path_extension(string_lit("c:/.stuff/..")), string_empty);
  }

  it("can retrieve the stem of a path") {
    check_eq_string(path_stem(string_lit("note.txt")), string_lit("note"));
    check_eq_string(path_stem(string_lit("note.txt.back")), string_lit("note"));
    check_eq_string(path_stem(string_lit("note")), string_lit("note"));
    check_eq_string(path_stem(string_lit("note.")), string_lit("note"));
    check_eq_string(path_stem(string_lit("c:/.stuff/note.txt")), string_lit("note"));
    check_eq_string(path_stem(string_lit("c:/.stuff/.")), string_empty);
  }

  it("can retrieve the parent of a path") {
    check_eq_string(path_parent(string_lit("How/You/Doing")), string_lit("How/You"));
    check_eq_string(path_parent(string_lit("stuff")), string_lit(""));
    check_eq_string(path_parent(string_lit("stuff/")), string_lit("stuff"));
    check_eq_string(path_parent(string_lit("c:/stuff")), string_lit("c:/"));
    check_eq_string(path_parent(string_lit("c:/stuff/note.txt")), string_lit("c:/stuff"));
    check_eq_string(path_parent(string_lit("c:/")), string_lit("c:/"));
    check_eq_string(path_parent(string_lit("/")), string_lit("/"));
    check_eq_string(path_parent(string_lit("/Stuff")), string_lit("/"));
  }

  it("can canonize paths") {
    struct {
      String path;
      String expected;
    } const data[] = {
        {string_lit("/"), string_lit("/")},
        {string_lit("/Hello World"), string_lit("/Hello World")},
        {string_lit("C:\\"), string_lit("C:/")},
        {string_lit("C:/"), string_lit("C:/")},
        {string_lit("c:\\"), string_lit("C:/")},
        {string_lit("c:/"), string_lit("C:/")},
        {string_lit("c:\\Hello World"), string_lit("C:/Hello World")},
        {string_lit("/How/You/Doing"), string_lit("/How/You/Doing")},
        {string_lit("How/You/Doing"), string_lit("How/You/Doing")},
        {string_lit("How/You/Doing/"), string_lit("How/You/Doing")},
        {string_lit("How/You/Doing//"), string_lit("How/You/Doing")},
        {string_lit(".How/..You/...Doing/."), string_lit(".How/..You/...Doing")},
        {string_lit("How/./Doing"), string_lit("How/Doing")},
        {string_lit("How/././././Doing"), string_lit("How/Doing")},
        {string_lit("How///Doing"), string_lit("How/Doing")},
        {string_lit("How/You/../Doing/../You/Doing"), string_lit("How/You/Doing")},
        {string_lit("/How/You/../Doing/../You/Doing"), string_lit("/How/You/Doing")},
        {string_lit("c:/How/You/../Doing/../You/Doing"), string_lit("C:/How/You/Doing")},
        {string_lit("Hello/How/.//.//../You"), string_lit("Hello/You")},
        {string_lit("How/../You/../Doing"), string_lit("Doing")},
        {string_lit("How/../..\\../Doing"), string_lit("Doing")},
        {string_lit("../..\\.."), string_lit("")},
        {string_lit("/..\\../.."), string_lit("/")},
        {string_lit("C:\\..\\..\\.."), string_lit("C:/")},
        {string_lit("\\Hello"), string_lit("Hello")},
    };

    DynString string = dynstring_create_over(mem_stack(128));
    for (usize i = 0; i != array_elems(data); ++i) {
      dynstring_clear(&string);
      path_canonize(&string, data[i].path);
      check_eq_string(dynstring_view(&string), data[i].expected);
    }
    dynstring_destroy(&string);
  }

  it("can append paths together") {
    DynString string = dynstring_create_over(mem_stack(128));

    path_append(&string, string_lit("Hello"));
    path_append(&string, string_lit("How"));
    path_append(&string, string_lit("You"));
    path_append(&string, string_lit("Doing?"));

    check_eq_string(dynstring_view(&string), string_lit("Hello/How/You/Doing?"));

    dynstring_destroy(&string);
  }

  it("returns the working-dir when building a path from 0 segments") {
    check_eq_string(path_build_scratch(), g_path_workingdir);
  }

  it("prepends the working-dir when building a path starting from a relative segment") {
    check_eq_string(
        path_build_scratch(string_lit("hello")),
        fmt_write_scratch("{}/hello", fmt_text(g_path_workingdir)));
  }

  it("doesn't prepend the working-dir when building a path starting from an absolute segment") {
    check_eq_string(path_build_scratch(string_lit("/hello")), string_lit("/hello"));
  }

  it("supports building paths from a collection of segments") {
    check_eq_string(
        path_build_scratch(string_lit("how\\are/you"), string_lit("doing")),
        fmt_write_scratch("{}/how/are/you/doing", fmt_text(g_path_workingdir)));
  }

  it("can generate a random file-name") {
    static const u64 seed = 42;

    Allocator* alloc = alloc_bump_create_stack(256);
    Rng*       rng   = rng_create_xorwow(alloc, seed);

    check_eq_string(path_name_random_scratch(rng, string_empty), string_lit("nkOZrR4b15bJ"));
    check_eq_string(
        path_name_random_scratch(rng, string_lit("hello")), string_lit("hello_ecfcmkK1mPyR"));
  }

  it("can generate a timestampped file-name") {
    const String nameWithoutPrefix = path_name_timestamp_scratch(string_empty);
    check_eq_int(nameWithoutPrefix.size, 15);
    const String nameWithPrefix = path_name_timestamp_scratch(string_lit("hello"));
    check_eq_int(nameWithPrefix.size, 21);
  }

  it("can retrieve the executable path") {
    check(!string_is_empty(g_path_executable));
    check(path_is_absolute(g_path_executable));
  }

  it("can retrieve the working-directory path") {
    check(!string_is_empty(g_path_workingdir));
    check(path_is_absolute(g_path_workingdir));
  }

  it("can retrieve the system temp path") {
    check(!string_is_empty(g_path_tempdir));
    check(path_is_absolute(g_path_tempdir));
  }
}
