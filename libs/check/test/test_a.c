#include "check_spec.h"

spec(a) {

  it("should be cool") { check_eq_int(42, 41 + 1); }

  skip_it("should work") { check_eq_int(42, 41 + 2); }
}
