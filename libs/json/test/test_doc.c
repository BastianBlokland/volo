#include "json_doc.h"

#include "check_spec.h"

spec(doc) {

  JsonDoc* doc = null;

  setup() { doc = json_create(g_alloc_heap, 0); }

  it("can retrieve the value of a string") {
    const JsonVal str = json_add_string_lit(doc, "Hello World");

    check_eq_int(json_type(doc, str), JsonType_String);
    check_eq_string(json_string(doc, str), string_lit("Hello World"));
  }

  it("can store an empty string") {
    const JsonVal str = json_add_string(doc, string_empty);

    check_eq_int(json_type(doc, str), JsonType_String);
    check_eq_string(json_string(doc, str), string_empty);
  }

  it("can retrieve the value of a number") {
    const JsonVal val = json_add_number(doc, 42.1337);

    check_eq_int(json_type(doc, val), JsonType_Number);
    check_eq_float(json_number(doc, val), 42.1337, .1e-32);
  }

  it("can retrieve the value of a boolean") {
    const JsonVal val = json_add_bool(doc, true);

    check_eq_int(json_type(doc, val), JsonType_Bool);
    check(json_bool(doc, val));
  }

  it("can retrieve the type of null") {
    const JsonVal val = json_add_null(doc);

    check_eq_int(json_type(doc, val), JsonType_Null);
  }

  it("can store empty arrays") {
    const JsonVal val = json_add_array(doc);

    check_eq_int(json_type(doc, val), JsonType_Array);
    check_eq_int(json_elem_count(doc, val), 0);
    check(sentinel_check(json_elem_begin(doc, val))); // Check that there is no first element.
  }

  it("can store arrays with a single element") {
    const JsonVal val  = json_add_array(doc);
    const JsonVal elem = json_add_string_lit(doc, "Hello World");

    check_eq_int(json_parent(doc, elem), JsonParent_None);

    json_add_elem(doc, val, elem);

    check_eq_int(json_parent(doc, elem), JsonParent_Array);

    check_eq_int(json_type(doc, val), JsonType_Array);
    check_eq_int(json_elem_count(doc, val), 1);
    check_eq_int(json_elem_begin(doc, val), elem);
    check(sentinel_check(json_elem_next(doc, elem))); // Check that there is no second element.
  }

  it("can lookup array elements by index") {
    const JsonVal val = json_add_array(doc);

    const JsonVal elemVal1 = json_add_string_lit(doc, "Hello");
    const JsonVal elemVal2 = json_add_bool(doc, true);
    const JsonVal elemVal3 = json_add_null(doc);

    json_add_elem(doc, val, elemVal1);
    json_add_elem(doc, val, elemVal2);
    json_add_elem(doc, val, elemVal3);

    check_eq_int(json_elem(doc, val, 0), elemVal1);
    check_eq_int(json_elem(doc, val, 1), elemVal2);
    check_eq_int(json_elem(doc, val, 2), elemVal3);
    check(sentinel_check(json_elem(doc, val, 3)));
    check(sentinel_check(json_elem(doc, val, 42)));
  }

  it("can iterate array elements") {
    const JsonVal val = json_add_array(doc);

    json_add_elem(doc, val, json_add_string_lit(doc, "Hello World"));
    json_add_elem(doc, val, json_add_bool(doc, true));
    json_add_elem(doc, val, json_add_null(doc));

    i32 i = 0;
    json_for_elems(doc, val, elem, {
      switch (i++) {
      case 0:
        check_eq_int(json_type(doc, elem), JsonType_String);
        break;
      case 1:
        check_eq_int(json_type(doc, elem), JsonType_Bool);
        break;
      case 2:
        check_eq_int(json_type(doc, elem), JsonType_Null);
        break;
      default:
        check(false);
      }
    });

    check_eq_int(json_elem_count(doc, val), 3);
  }

  it("can store empty objects") {
    const JsonVal val = json_add_object(doc);

    check_eq_int(json_type(doc, val), JsonType_Object);
    check_eq_int(json_field_count(doc, val), 0);

    // Check that there is no first field.
    check_eq_string(json_field_begin(doc, val).name, string_empty);
    check(sentinel_check(json_field_begin(doc, val).value));
  }

  it("can store objects with a single field") {
    const JsonVal val   = json_add_object(doc);
    const JsonVal field = json_add_string_lit(doc, "Hello World");

    check_eq_int(json_parent(doc, field), JsonParent_None);

    const bool res = json_add_field_str(doc, val, string_lit("a"), field);
    check(res);

    check_eq_int(json_parent(doc, field), JsonParent_Object);

    check_eq_int(json_type(doc, val), JsonType_Object);
    check_eq_int(json_field_count(doc, val), 1);
    check_eq_string(json_field_begin(doc, val).name, string_lit("a"));
    check_eq_int(json_field_begin(doc, val).value, field);
    check(
        sentinel_check(json_field_next(doc, field).value)); // Check that there is no second field.
  }

  it("can lookup object fields by name") {
    const JsonVal val = json_add_object(doc);

    const JsonVal fieldVal1 = json_add_string_lit(doc, "Hello");
    const JsonVal fieldVal2 = json_add_bool(doc, true);
    const JsonVal fieldVal3 = json_add_null(doc);

    json_add_field_str(doc, val, string_lit("a"), fieldVal1);
    json_add_field_str(doc, val, string_lit("b"), fieldVal2);
    json_add_field_str(doc, val, string_lit("c"), fieldVal3);

    check_eq_int(json_field(doc, val, string_lit("a")), fieldVal1);
    check_eq_int(json_field(doc, val, string_lit("b")), fieldVal2);
    check_eq_int(json_field(doc, val, string_lit("c")), fieldVal3);
    check(sentinel_check(json_field(doc, val, string_lit("d"))));
    check(sentinel_check(json_field(doc, val, string_lit(""))));
  }

  it("can iterate object fields") {
    const JsonVal val = json_add_object(doc);

    bool res = true;
    res &= json_add_field_str(doc, val, string_lit("a"), json_add_string_lit(doc, "Hello"));
    res &= json_add_field_str(doc, val, string_lit("b"), json_add_bool(doc, true));
    res &= json_add_field_str(doc, val, string_lit("c"), json_add_null(doc));
    check(res);

    i32 i = 0;
    json_for_fields(doc, val, itr, {
      switch (i++) {
      case 0:
        check_eq_string(itr.name, string_lit("a"));
        check_eq_int(json_type(doc, itr.value), JsonType_String);
        break;
      case 1:
        check_eq_string(itr.name, string_lit("b"));
        check_eq_int(json_type(doc, itr.value), JsonType_Bool);
        break;
      case 2:
        check_eq_string(itr.name, string_lit("c"));
        check_eq_int(json_type(doc, itr.value), JsonType_Null);
        break;
      default:
        check(false);
      }
    });

    check_eq_int(json_field_count(doc, val), 3);
  }

  it("returns false when adding two fields with the same name to an object") {
    const JsonVal val = json_add_object(doc);

    check(json_add_field_str(doc, val, string_lit("a"), json_add_null(doc)));
    check(!json_add_field_str(doc, val, string_lit("a"), json_add_null(doc)));
    check(!json_add_field_str(doc, val, string_lit("a"), json_add_number(doc, 42)));
    check(json_add_field_str(doc, val, string_lit("b"), json_add_null(doc)));
  }

  it("can store complex structures") {
    const JsonVal obj1 = json_add_object(doc);
    json_add_field_str(doc, obj1, string_lit("a"), json_add_null(doc));
    json_add_field_str(doc, obj1, string_lit("b"), json_add_string_lit(doc, "Hello"));

    const JsonVal arr = json_add_array(doc);
    json_add_elem(doc, arr, json_add_bool(doc, true));
    json_add_elem(doc, arr, json_add_bool(doc, false));
    json_add_elem(doc, arr, obj1);

    const JsonVal obj2 = json_add_object(doc);
    json_add_field_str(doc, obj2, string_lit("a"), json_add_null(doc));

    const JsonVal root = json_add_object(doc);
    json_add_field_str(doc, root, string_lit("array"), arr);
    json_add_field_str(doc, root, string_lit("num"), json_add_number(doc, 42.0));
    json_add_field_str(doc, root, string_lit("subObj"), obj2);

    i32 i = 0;
    json_for_fields(doc, root, rootItr, {
      switch (i++) {
      case 0: {
        const JsonVal elem0 = json_elem(doc, rootItr.value, 0);
        const JsonVal elem1 = json_elem(doc, rootItr.value, 1);
        const JsonVal elem2 = json_elem(doc, rootItr.value, 2);

        check(json_bool(doc, elem0));
        check(!json_bool(doc, elem1));
        check_eq_int(json_type(doc, json_field(doc, elem2, string_lit("a"))), JsonType_Null);
        check_eq_string(
            json_string(doc, json_field(doc, elem2, string_lit("b"))), string_lit("Hello"));
      } break;
      case 1:
        check_eq_float(json_number(doc, rootItr.value), 42.0, .1e-32);
        break;
      case 2:
        check_eq_int(
            json_type(doc, json_field(doc, rootItr.value, string_lit("a"))), JsonType_Null);
        break;
      }
    });
  }

  teardown() { json_destroy(doc); }
}
