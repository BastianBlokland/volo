#pragma once
#include "ai_value.h"
#include "check_spec.h"

// clang-format off

#define check_eq_value(_A_, _B_)      check_eq_value_impl(_testCtx, (_A_), (_B_), source_location())
#define check_neq_value(_A_, _B_)     check_neq_value_impl(_testCtx, (_A_), (_B_), source_location())
#define check_less_value(_A_, _B_)    check_less_value_impl(_testCtx, (_A_), (_B_), source_location())
#define check_greater_value(_A_, _B_) check_greater_value_impl(_testCtx, (_A_), (_B_), source_location())

// clang-format on

void check_eq_value_impl(CheckTestContext*, AiValue a, AiValue b, SourceLoc);
void check_neq_value_impl(CheckTestContext*, AiValue a, AiValue b, SourceLoc);
void check_less_value_impl(CheckTestContext*, AiValue a, AiValue b, SourceLoc);
void check_greater_value_impl(CheckTestContext*, AiValue a, AiValue b, SourceLoc);
