#include "ai_value.h"
#include "check_spec.h"

spec(value) {
  it("can produce a textual representation for a type") {
    check_eq_string(ai_value_type_str(AiValueType_f64), string_lit("f64"));
    check_eq_string(ai_value_type_str(AiValueType_Bool), string_lit("bool"));
    check_eq_string(ai_value_type_str(AiValueType_Vector), string_lit("vector"));
    check_eq_string(ai_value_type_str(AiValueType_Time), string_lit("time"));
    check_eq_string(ai_value_type_str(AiValueType_Entity), string_lit("entity"));
  }
}
