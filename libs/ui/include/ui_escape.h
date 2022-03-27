#pragma once
#include "core_format.h"
#include "ui_color.h"
#include "ui_units.h"

/**
 * Escape sequences can be used to control various aspects of text rendering.
 *
 * Supported sequences:
 * - Reset the style to default:  [ESC]r           example: \ar
 * - Switch to a specific color:  [ESC]#RRGGBBAA   example: \a#FF0000FF
 * - Switch to a named color:     [ESC]~NAME       example: \a~red
 * - Switch the outline width:    [ESC]|FF         example: \a|10
 * - Switch to 'light' weight:    [ESC].l          example: \a.l
 * - Switch to 'normal' weight:   [ESC].n          example: \a.n
 * - Switch to 'bold' weight:     [ESC].b          example: \a.b
 * - Switch to 'heavy' weight:    [ESC].h          example: \a.h
 *
 * NOTE: The 'Bell' character is supported as an alternative to the normal 'ESC' character. Reason
 * is C has \a shorthand for the bell character.
 */

/**
 * Create a formatting argument that contains a color escape sequence.
 * NOTE: Resulting string is allocated in scratch memory, should NOT be stored.
 */
#define fmt_ui_color(_COLOR_) fmt_text(ui_escape_color_scratch(_COLOR_))

/**
 * Create a formatting argument that contains an outline escape sequence.
 * NOTE: Resulting string is allocated in scratch memory, should NOT be stored.
 */
#define fmt_ui_outline(_OUTLINE_) fmt_text(ui_escape_outline_scratch(_OUTLINE_))

/**
 * Create a formatting argument that contains an weight escape sequence.
 * NOTE: Resulting string is allocated in scratch memory, should NOT be stored.
 */
#define fmt_ui_weight(_WEIGHT_) fmt_text(ui_escape_weight_scratch(_WEIGHT_))

String ui_escape_color_scratch(UiColor);
String ui_escape_outline_scratch(u8);
String ui_escape_weight_scratch(UiWeight);
