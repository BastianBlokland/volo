#include "core_diag.h"
#include "core_path.h"

static void test_path_is_absolute() {
  diag_assert(path_is_absolute(string_lit("/")));
  diag_assert(path_is_absolute(string_lit("c:/")));
  diag_assert(path_is_absolute(string_lit("C:/")));
  diag_assert(path_is_absolute(string_lit("C:\\")));

  diag_assert(!path_is_absolute(string_lit("Hello")));
  diag_assert(!path_is_absolute(string_lit("./")));
  diag_assert(!path_is_absolute(string_lit("../")));
  diag_assert(!path_is_absolute(string_lit("\\")));
}

static void test_path_is_root() {
  diag_assert(path_is_root(string_lit("/")));
  diag_assert(path_is_root(string_lit("c:/")));
  diag_assert(path_is_root(string_lit("C:/")));
  diag_assert(path_is_root(string_lit("C:\\")));

  diag_assert(!path_is_root(string_lit("Hello")));
  diag_assert(!path_is_root(string_lit("/Hello")));
  diag_assert(!path_is_root(string_lit("c:/Hello")));
}

static void test_path_filename() {
  diag_assert(string_eq(path_filename(string_lit("note.txt")), string_lit("note.txt")));
  diag_assert(string_eq(path_filename(string_lit("/stuff/note.txt")), string_lit("note.txt")));
  diag_assert(string_eq(path_filename(string_lit("c:/stuff/note.txt")), string_lit("note.txt")));
  diag_assert(string_eq(path_filename(string_lit("c:/stuff/")), string_empty));
  diag_assert(string_eq(path_filename(string_lit("/")), string_empty));
}

static void test_path_extension() {
  diag_assert(string_eq(path_extension(string_lit("note.txt")), string_lit("txt")));
  diag_assert(string_eq(path_extension(string_lit("note.txt.back")), string_lit("back")));
  diag_assert(string_eq(path_extension(string_lit("c:/.stuff/note.txt")), string_lit("txt")));
  diag_assert(string_eq(path_extension(string_lit("c:/.stuff/note")), string_empty));
  diag_assert(string_eq(path_extension(string_lit("c:/.stuff/note.")), string_empty));
  diag_assert(string_eq(path_extension(string_lit("c:/.stuff/.")), string_empty));
  diag_assert(string_eq(path_extension(string_lit("c:/.stuff/..")), string_empty));
}

static void test_path_stem() {
  diag_assert(string_eq(path_stem(string_lit("note.txt")), string_lit("note")));
  diag_assert(string_eq(path_stem(string_lit("note.txt.back")), string_lit("note")));
  diag_assert(string_eq(path_stem(string_lit("note")), string_lit("note")));
  diag_assert(string_eq(path_stem(string_lit("note.")), string_lit("note")));
  diag_assert(string_eq(path_stem(string_lit("c:/.stuff/note.txt")), string_lit("note")));
  diag_assert(string_eq(path_stem(string_lit("c:/.stuff/.")), string_empty));
}

static void test_path_parent() {
  diag_assert(string_eq(path_parent(string_lit("How/You/Doing")), string_lit("How/You")));
  diag_assert(string_eq(path_parent(string_lit("stuff")), string_lit("")));
  diag_assert(string_eq(path_parent(string_lit("stuff/")), string_lit("stuff")));
  diag_assert(string_eq(path_parent(string_lit("c:/stuff")), string_lit("c:/")));
  diag_assert(string_eq(path_parent(string_lit("c:/stuff/note.txt")), string_lit("c:/stuff")));
  diag_assert(string_eq(path_parent(string_lit("c:/")), string_lit("c:/")));
  diag_assert(string_eq(path_parent(string_lit("/")), string_lit("/")));
  diag_assert(string_eq(path_parent(string_lit("/Stuff")), string_lit("/")));
}

static void test_path_canonize(const String path, const String expected) {
  DynString string = dynstring_create_over(mem_stack(128));

  path_canonize(&string, path);
  diag_assert(string_eq(dynstring_view(&string), expected));

  dynstring_destroy(&string);
}

static void test_path_append() {
  DynString string = dynstring_create_over(mem_stack(128));

  path_append(&string, string_lit("Hello"));
  path_append(&string, string_lit("How"));
  path_append(&string, string_lit("You"));
  path_append(&string, string_lit("Doing?"));

  diag_assert(string_eq(dynstring_view(&string), string_lit("Hello/How/You/Doing?")));

  dynstring_destroy(&string);
}

void test_path() {
  test_path_is_absolute();
  test_path_is_root();
  test_path_filename();
  test_path_extension();
  test_path_stem();
  test_path_parent();

  test_path_canonize(string_lit("/"), string_lit("/"));
  test_path_canonize(string_lit("/Hello World"), string_lit("/Hello World"));
  test_path_canonize(string_lit("C:\\"), string_lit("C:/"));
  test_path_canonize(string_lit("C:/"), string_lit("C:/"));
  test_path_canonize(string_lit("c:\\"), string_lit("C:/"));
  test_path_canonize(string_lit("c:/"), string_lit("C:/"));
  test_path_canonize(string_lit("c:\\Hello World"), string_lit("C:/Hello World"));
  test_path_canonize(string_lit("/How/You/Doing"), string_lit("/How/You/Doing"));
  test_path_canonize(string_lit("How/You/Doing"), string_lit("How/You/Doing"));
  test_path_canonize(string_lit("How/You/Doing/"), string_lit("How/You/Doing"));
  test_path_canonize(string_lit("How/You/Doing//"), string_lit("How/You/Doing"));
  test_path_canonize(string_lit(".How/..You/...Doing/."), string_lit(".How/..You/...Doing"));
  test_path_canonize(string_lit("How/./Doing"), string_lit("How/Doing"));
  test_path_canonize(string_lit("How/././././Doing"), string_lit("How/Doing"));
  test_path_canonize(string_lit("How///Doing"), string_lit("How/Doing"));
  test_path_canonize(string_lit("How/You/../Doing/../You/Doing"), string_lit("How/You/Doing"));
  test_path_canonize(string_lit("/How/You/../Doing/../You/Doing"), string_lit("/How/You/Doing"));
  test_path_canonize(
      string_lit("c:/How/You/../Doing/../You/Doing"), string_lit("C:/How/You/Doing"));
  test_path_canonize(string_lit("Hello/How/.//.//../You"), string_lit("Hello/You"));
  test_path_canonize(string_lit("How/../You/../Doing"), string_lit("Doing"));
  test_path_canonize(string_lit("How/../..\\../Doing"), string_lit("Doing"));
  test_path_canonize(string_lit("../..\\.."), string_lit(""));
  test_path_canonize(string_lit("/..\\../.."), string_lit("/"));
  test_path_canonize(string_lit("C:\\..\\..\\.."), string_lit("C:/"));
  test_path_canonize(string_lit("\\Hello"), string_lit("Hello"));

  test_path_append();
}
