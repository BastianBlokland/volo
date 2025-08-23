#include "check/spec.h"
#include "core/complex.h"

spec(complex) {

  it("can be multiplied") {
    const Complex a = complex(2, 3);
    const Complex b = complex(4, 5);
    const Complex c = complex_mul(a, b);
    check_eq_float(c.real, -7, 1e-10);
    check_eq_float(c.imaginary, 22, 1e-10);
  }

  it("lists both components when formatted") {
    const Complex val = complex(2, 3);
    check_eq_string(fmt_write_scratch("{}", complex_fmt(val)), string_lit("2, 3"));
  }
}
