#pragma once
#include "core_format.h"
#include "ui_color.h"

/**
 * Escape sequences can be used to control various aspects of text rendering.
 *
 * Supported sequences:
 * - Switch to a specific color: [ESC]#RRGGBBAA
 *
 * NOTE: The 'Bell' character is supported as an alternative to the normal 'ESC' character. Reason
 * is C has \a shorthand for the bell character.
 *
 * Example usage:
 * "Hello \a#FF0000FFWorld": Display 'Hello World' where 'World' is rendered in red.
 */

/**
 * Create a formatting argument that contains a color escape sequence.
 * NOTE: Resulting string is allocated in scratch memory, should NOT be stored.
 */
#define fmt_ui_color(_COLOR_) fmt_text(ui_escape_color_scratch(_COLOR_))

String ui_escape_color_scratch(UiColor);
