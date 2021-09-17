#pragma once
#include "check_def.h"

/**
 * Run the given TestSuite definition as an application.
 * Supports providing command-line arguments and returns an exit-code.
 */
int check_app(CheckDef*, int argc, const char** argv);
