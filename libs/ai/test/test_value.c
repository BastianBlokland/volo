#include "ai_value.h"
#include "check_spec.h"
#include "core_time.h"

spec(value) {
  it("can type-erase values") {
    check_eq_int(ai_value_f64(42).type, AiValueType_f64);
    check_eq_int(ai_value_f64(42).data_f64, 42);

    check_eq_int(ai_value_bool(true).type, AiValueType_Bool);
    check(ai_value_bool(true).data_bool == true);

    check_eq_int(ai_value_vector(geo_vector(1, 2, 3)).type, AiValueType_Vector);
    check_eq_int(ai_value_vector(geo_vector(1, 2, 3)).data_vector.z, 3);

    check_eq_int(ai_value_time(time_seconds(2)).type, AiValueType_Time);
    check_eq_int(ai_value_time(time_seconds(2)).data_time, time_seconds(2));

    check_eq_int(ai_value_entity(42).type, AiValueType_Entity);
    check_eq_int(ai_value_entity(42).data_entity, 42);
  }

  it("can produce a textual representation for a type") {
    check_eq_string(ai_value_type_str(AiValueType_f64), string_lit("f64"));
    check_eq_string(ai_value_type_str(AiValueType_Bool), string_lit("bool"));
    check_eq_string(ai_value_type_str(AiValueType_Vector), string_lit("vector"));
    check_eq_string(ai_value_type_str(AiValueType_Time), string_lit("time"));
    check_eq_string(ai_value_type_str(AiValueType_Entity), string_lit("entity"));
  }
}
