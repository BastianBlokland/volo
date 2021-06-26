#include "check_spec.h"

spec(a) {

  it("should be cool") { check_eq_int(42, 41 + 1); }

  it("should work") { check_eq_int(42, 41 + 1); }
}
