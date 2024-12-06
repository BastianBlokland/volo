#pragma once
#include "core_diag.h"
#include "core_diag_except.h"

void diag_crash_report(const SymbolStack*, String msg);

void          diag_pal_except_enable(jmp_buf* anchor, i32 code);
void          diag_pal_except_disable(void);
void          diag_pal_break(void);
NORETURN void diag_pal_crash(void);
