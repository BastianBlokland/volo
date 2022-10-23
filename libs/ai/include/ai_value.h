#pragma once
#include "core_string.h"
#include "ecs_entity.h"
#include "geo_vector.h"

// Forward declare from 'core_time.h'.
typedef i64 TimeDuration;

typedef enum {
  AiValueType_None,
  AiValueType_f64,
  AiValueType_Bool,
  AiValueType_Vector,
  AiValueType_Time,
  AiValueType_Entity,

  AiValueType_Count,
} AiValueType;

typedef struct {
  AiValueType type;
  union {
    f64          data_f64;
    bool         data_bool;
    GeoVector    data_vector;
    TimeDuration data_time;
    EcsEntityId  data_entity;
  };
} AiValue;

/**
 * Type-erase a value into a AiValue.
 */
AiValue ai_value_none();
AiValue ai_value_f64(f64);
AiValue ai_value_bool(bool);
AiValue ai_value_vector(GeoVector);
AiValue ai_value_time(TimeDuration);
AiValue ai_value_entity(EcsEntityId);

/**
 * Create a textual representation of a value.
 */
String ai_value_type_str(AiValueType);
String ai_value_str_scratch(AiValue);

/**
 * Compare values.
 */
bool ai_value_equal(AiValue, AiValue);
bool ai_value_less(AiValue, AiValue);
bool ai_value_greater(AiValue, AiValue);
