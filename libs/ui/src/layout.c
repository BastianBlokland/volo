#include "ui_layout.h"

void ui_layout_to_center(UiCanvasComp* canvas, const UiAxis axis) {
  ui_canvas_rect_move(canvas, ui_vector(0.5f, 0.5f), UiOrigin_Current, UiUnits_Current, axis);
}

void ui_layout_from_center(UiCanvasComp* canvas, const UiAxis axis) {
  ui_canvas_rect_move(canvas, ui_vector(-0.5f, -0.5f), UiOrigin_Current, UiUnits_Current, axis);
}

void ui_layout_set_rect(UiCanvasComp* canvas, const UiRect rect) {
  ui_canvas_rect_move(canvas, rect.pos, UiOrigin_WindowBottomLeft, UiUnits_Absolute, Ui_XY);
  ui_canvas_rect_resize(canvas, rect.size, UiUnits_Absolute, Ui_XY);
}
