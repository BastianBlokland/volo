#include "check_spec.h"

spec(b) {

  it("Must do stuff") { check_eq_int(42, 41 + 1); }

  it("could work well??") { check_eq_int(42, 41 + 2); }
}
