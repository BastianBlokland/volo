#include "ui_layout.h"

void ui_layout_to_center(UiCanvasComp* canvas, const UiAxis axis) {
  ui_canvas_rect_pos(canvas, UiBase_Current, ui_vector(0.5f, 0.5f), UiBase_Current, axis);
}

void ui_layout_from_center(UiCanvasComp* canvas, const UiAxis axis) {
  ui_canvas_rect_pos(canvas, UiBase_Current, ui_vector(-0.5f, -0.5f), UiBase_Current, axis);
}

void ui_layout_set_rect(UiCanvasComp* canvas, const UiRect rect) {
  ui_canvas_rect_pos(canvas, UiBase_Absolute, rect.pos, UiBase_Absolute, Ui_XY);
  ui_canvas_rect_size(canvas, rect.size, UiBase_Absolute, Ui_XY);
}
