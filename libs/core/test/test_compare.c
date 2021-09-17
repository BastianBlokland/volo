#include "check_spec.h"
#include "core_compare.h"
#include "core_string.h"

spec(compare) {

  it("can compare primitive types") {
    check_eq_int(compare_i32(&(i32){1}, &(i32){2}), -1);
    check_eq_int(compare_i32(&(i32){1}, &(i32){1}), 0);
    check_eq_int(compare_i32(&(i32){2}, &(i32){1}), 1);
    check_eq_int(compare_i32(&(i32){-2}, &(i32){-1}), -1);
    check_eq_int(compare_i32(&(i32){-2}, &(i32){-3}), 1);
    check_eq_int(compare_i32(&(i32){-2}, &(i32){-2}), 0);

    check_eq_int(compare_i32_reverse(&(i32){1}, &(i32){2}), 1);
    check_eq_int(compare_i32_reverse(&(i32){-2}, &(i32){-1}), 1);
    check_eq_int(compare_i32_reverse(&(i32){-2}, &(i32){-3}), -1);
    check_eq_int(compare_i32_reverse(&(i32){-2}, &(i32){-2}), 0);

    check_eq_int(compare_u32(&(u32){42}, &(u32){1337}), -1);
    check_eq_int(compare_u32(&(u32){1337}, &(u32){42}), 1);

    check_eq_int(compare_u32_reverse(&(u32){42}, &(u32){1337}), 1);
    check_eq_int(compare_u32_reverse(&(u32){1337}, &(u32){42}), -1);

    check_eq_int(compare_f32(&(f32){1.1f}, &(f32){1.3f}), -1);
    check_eq_int(compare_f32(&(f32){1.3f}, &(f32){1.1f}), 1);
    check_eq_int(compare_f32(&(f32){1.3f}, &(f32){1.3f}), 0);

    check_eq_int(compare_f32_reverse(&(f32){1.1f}, &(f32){1.3f}), 1);
    check_eq_int(compare_f32_reverse(&(f32){1.3f}, &(f32){1.1f}), -1);
    check_eq_int(compare_f32_reverse(&(f32){1.3f}, &(f32){1.3f}), 0);

    check_eq_int(compare_string(&string_lit("a"), &string_lit("b")), -1);
    check_eq_int(compare_string(&string_lit("a"), &string_lit("a")), 0);
    check_eq_int(compare_string(&string_lit("b"), &string_lit("a")), 1);

    check_eq_int(compare_string_reverse(&string_lit("a"), &string_lit("b")), 1);
    check_eq_int(compare_string_reverse(&string_lit("a"), &string_lit("a")), 0);
    check_eq_int(compare_string_reverse(&string_lit("b"), &string_lit("a")), -1);
  }
}
