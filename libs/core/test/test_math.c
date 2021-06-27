#include "core_math.h"

#include "check_spec.h"

spec(math) {

  it("can compute the min argument") {
    check_eq_int(math_min(1, 0), 0);
    check_eq_int(math_min(0, 0), 0);
    check_eq_int(math_min(1, -1), -1);
    check_eq_int(math_min(-1, 0), -1);

    check_eq_float(math_min(-1.0f, 0.0f), -1.0f, 1e-6);
    check_eq_float(math_min(-1.1f, -1.2f), -1.2f, 1e-6);
  }

  it("can compute the max argument") {
    check_eq_int(math_max(1, 0), 1);
    check_eq_int(math_max(0, 0), 0);
    check_eq_int(math_max(-1, 1), 1);
    check_eq_int(math_max(-1, -2), -1);

    check_eq_float(math_max(-1.0f, 0.1f), 0.1f, 1e-6);
    check_eq_float(math_max(-1.1f, -1.2f), -1.1f, 1e-6);
  }

  it("can compute the sign of the argument") {
    check_eq_int(math_sign(-42), -1);
    check_eq_int(math_sign(42), 1);
    check_eq_int(math_sign(0), 0);

    check_eq_int(math_sign(-0.1f), -1);
    check_eq_int(math_sign(0.1f), 1);
    check_eq_int(math_sign(0.0f), 0);
  }

  it("can compute the absolute of the argument") {
    check_eq_int(math_abs(-42), 42);
    check_eq_int(math_abs(42), 42);
    check_eq_int(math_abs(0), 0);
    check_eq_float(math_abs(-1.25), 1.25, 1e-6);
    check_eq_float(math_abs(0.0), 0.0, 1e-6);
  }
}
