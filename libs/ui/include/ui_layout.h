#pragma once
#include "ui_canvas.h"

/**
 * Update the rectangle so that the origin (lower-left) is in the center of the previous rectangle.
 */
void ui_layout_to_center(UiCanvasComp*, UiAxis);

/**
 * Update the rectangle so that the center is at the lower-left of the previous rectangle.
 */
void ui_layout_from_center(UiCanvasComp*, UiAxis);
