#pragma once
#include "check_spec.h"
#include "geo.h"

#define check_eq_quat(_A_, _B_) check_eq_quat_impl(_testCtx, _A_, _B_)
#define check_eq_vector(_A_, _B_) check_eq_vector_impl(_testCtx, _A_, _B_)

void check_eq_quat_impl(CheckTestContext* _testCtx, const GeoQuat a, const GeoQuat b);
void check_eq_vector_impl(CheckTestContext* _testCtx, const GeoVector a, const GeoVector b);
