#pragma once
#include "core.h"

#include <setjmp.h>

/**
 * Diagnostic exception interception.
 * When an exception occurs (for example a segmentation-fault) in the current thread and exception
 * interception is active then the exception is reported like a crash (including a stack-trace)
 * before exiting the process.
 *
 * Example usage:
 * ```
 * jmp_buf exceptAnchor;
 * diag_except_enable(&exceptAnchor, setjmp(exceptAnchor));
 *
 * // Application code.
 *
 * diag_except_disable();
 * ```
 *
 * When an exception occurs we jump to the registered anchor higher in the stack with a code
 * indicating that an exception has occurred. Then 'diag_except_enable()' will report the crash and
 * exit the process when its called again. We use this mechanism as the exception/signal handlers
 * themselves are not a safe place to report the crash, for more information see:
 * https://man7.org/linux/man-pages/man7/signal-safety.7.html
 *
 * Before the anchor is popped off the stack make sure to call 'diag_except_disable()' to avoid
 * jumping to an invalid location.
 *
 * NOTE: Exception interception is per thread.
 * NOTE: Exceptions are always fatal, the process will exit after reporting the crash.
 * NOTE: Its important that 'jmp_buf' is initialized in a location that stays on the stack.
 * Pre-condition: core_init() has been called.
 */
void diag_except_enable(jmp_buf* anchor, i32 exceptionCode);
void diag_except_disable(void);
