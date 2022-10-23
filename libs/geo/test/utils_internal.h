#pragma once
#include "check_spec.h"
#include "geo.h"

#define check_eq_matrix(_A_, _B_) check_eq_matrix_impl(_testCtx, (_A_), (_B_), source_location())
#define check_eq_quat(_A_, _B_) check_eq_quat_impl(_testCtx, (_A_), (_B_), source_location())
#define check_eq_vector(_A_, _B_) check_eq_vector_impl(_testCtx, (_A_), (_B_), source_location())

void check_eq_matrix_impl(CheckTestContext*, GeoMatrix a, GeoMatrix b, SourceLoc);
void check_eq_quat_impl(CheckTestContext*, GeoQuat a, GeoQuat b, SourceLoc);
void check_eq_vector_impl(CheckTestContext*, GeoVector a, GeoVector b, SourceLoc);
