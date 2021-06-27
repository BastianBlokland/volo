#include "check_spec.h"

spec(a) {

  it("should be cool") { check_eq_int(42, 41 + 1); }

  it("should work", .flags = CheckTestFlags_Skip) { check_eq_int(42, 41 + 2); }
}
