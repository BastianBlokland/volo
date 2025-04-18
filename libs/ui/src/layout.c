#include "core_diag.h"
#include "ui_layout.h"

#include "canvas_internal.h"

void ui_layout_push(UiCanvasComp* canvas) {
  UiCmdBuffer* cmdBuffer = ui_canvas_cmd_buffer(canvas);
  ui_cmd_push_rect_push(cmdBuffer);
}

void ui_layout_pop(UiCanvasComp* canvas) {
  UiCmdBuffer* cmdBuffer = ui_canvas_cmd_buffer(canvas);
  ui_cmd_push_rect_pop(cmdBuffer);
}

void ui_layout_container_push(UiCanvasComp* canvas, const UiClip clip, const UiLayer layer) {
  UiCmdBuffer* cmdBuffer = ui_canvas_cmd_buffer(canvas);
  ui_cmd_push_container_push(cmdBuffer, clip, layer);
}

void ui_layout_container_pop(UiCanvasComp* canvas) {
  UiCmdBuffer* cmdBuffer = ui_canvas_cmd_buffer(canvas);
  ui_cmd_push_container_pop(cmdBuffer);
}

void ui_layout_move(
    UiCanvasComp* canvas, const UiVector offset, const UiBase units, const UiAxis axis) {

  UiCmdBuffer* cmdBuffer = ui_canvas_cmd_buffer(canvas);
  ui_cmd_push_rect_pos(cmdBuffer, UiBase_Current, offset, units, axis);
}

void ui_layout_move_dir(
    UiCanvasComp* canvas, const UiDir dir, const f32 value, const UiBase units) {
  switch (dir) {
  case Ui_Right:
    ui_layout_move(canvas, ui_vector(value, 0), units, Ui_X);
    break;
  case Ui_Left:
    ui_layout_move(canvas, ui_vector(-value, 0), units, Ui_X);
    break;
  case Ui_Up:
    ui_layout_move(canvas, ui_vector(0, value), units, Ui_Y);
    break;
  case Ui_Down:
    ui_layout_move(canvas, ui_vector(0, -value), units, Ui_Y);
    break;
  }
}

void ui_layout_move_to(
    UiCanvasComp* canvas, const UiBase base, const UiAlign align, const UiAxis axis) {

  UiCmdBuffer* cmdBuffer = ui_canvas_cmd_buffer(canvas);
  switch (align) {
  case UiAlign_TopLeft:
    ui_cmd_push_rect_pos(cmdBuffer, base, ui_vector(0.0, 1.0), base, axis);
    break;
  case UiAlign_TopCenter:
    ui_cmd_push_rect_pos(cmdBuffer, base, ui_vector(0.5, 1.0), base, axis);
    break;
  case UiAlign_TopRight:
    ui_cmd_push_rect_pos(cmdBuffer, base, ui_vector(1.0, 1.0), base, axis);
    break;
  case UiAlign_MiddleLeft:
    ui_cmd_push_rect_pos(cmdBuffer, base, ui_vector(0.0, 0.5), base, axis);
    break;
  case UiAlign_MiddleCenter:
    ui_cmd_push_rect_pos(cmdBuffer, base, ui_vector(0.5, 0.5), base, axis);
    break;
  case UiAlign_MiddleRight:
    ui_cmd_push_rect_pos(cmdBuffer, base, ui_vector(1.0, 0.5), base, axis);
    break;
  case UiAlign_BottomLeft:
    ui_cmd_push_rect_pos(cmdBuffer, base, ui_vector(0.0, 0.0), base, axis);
    break;
  case UiAlign_BottomCenter:
    ui_cmd_push_rect_pos(cmdBuffer, base, ui_vector(0.5, 0.0), base, axis);
    break;
  case UiAlign_BottomRight:
    ui_cmd_push_rect_pos(cmdBuffer, base, ui_vector(1.0, 0.0), base, axis);
    break;
  }
}

void ui_layout_next(UiCanvasComp* canvas, const UiDir dir, const f32 spacing) {
  ui_layout_move_dir(canvas, dir, 1, UiBase_Current);
  ui_layout_move_dir(canvas, dir, spacing, UiBase_Absolute);
}

void ui_layout_grow(
    UiCanvasComp*  canvas,
    const UiAlign  origin,
    const UiVector delta,
    const UiBase   units,
    const UiAxis   axis) {

  UiCmdBuffer* cmdBuffer = ui_canvas_cmd_buffer(canvas);
  ui_cmd_push_rect_size_grow(cmdBuffer, delta, units, axis);

  switch (origin) {
  case UiAlign_TopLeft:
    ui_cmd_push_rect_pos(cmdBuffer, UiBase_Current, ui_vector(0.0, -delta.y), units, axis);
    break;
  case UiAlign_TopCenter:
    ui_cmd_push_rect_pos(
        cmdBuffer, UiBase_Current, ui_vector(-delta.x * 0.5f, -delta.y), units, axis);
    break;
  case UiAlign_TopRight:
    ui_cmd_push_rect_pos(cmdBuffer, UiBase_Current, ui_vector(-delta.x, -delta.y), units, axis);
    break;
  case UiAlign_MiddleLeft:
    ui_cmd_push_rect_pos(cmdBuffer, UiBase_Current, ui_vector(0.0, -delta.y * 0.5f), units, axis);
    break;
  case UiAlign_MiddleCenter:
    ui_cmd_push_rect_pos(
        cmdBuffer, UiBase_Current, ui_vector(-delta.x * 0.5f, -delta.y * 0.5f), units, axis);
    break;
  case UiAlign_MiddleRight:
    ui_cmd_push_rect_pos(
        cmdBuffer, UiBase_Current, ui_vector(-delta.x, -delta.y * 0.5f), units, axis);
    break;
  case UiAlign_BottomLeft:
    break;
  case UiAlign_BottomCenter:
    ui_cmd_push_rect_pos(cmdBuffer, UiBase_Current, ui_vector(-delta.x * 0.5f, 0.0), units, axis);
    break;
  case UiAlign_BottomRight:
    ui_cmd_push_rect_pos(cmdBuffer, UiBase_Current, ui_vector(-delta.x, 0.0), units, axis);
    break;
  }
}

void ui_layout_resize(
    UiCanvasComp*  canvas,
    const UiAlign  origin,
    const UiVector size,
    const UiBase   units,
    const UiAxis   axis) {

  UiCmdBuffer* cmdBuffer = ui_canvas_cmd_buffer(canvas);
  diag_assert_msg(size.x >= 0.0f && size.y >= 0.0f, "Negative sizes are not supported");
  ui_cmd_push_rect_size(cmdBuffer, size, units, axis);

  switch (origin) {
  case UiAlign_TopLeft:
    ui_cmd_push_rect_pos(cmdBuffer, UiBase_Current, ui_vector(0.0, -1.0), UiBase_Current, axis);
    break;
  case UiAlign_TopCenter:
    ui_cmd_push_rect_pos(cmdBuffer, UiBase_Current, ui_vector(-0.5, -1.0), UiBase_Current, axis);
    break;
  case UiAlign_TopRight:
    ui_cmd_push_rect_pos(cmdBuffer, UiBase_Current, ui_vector(-1.0, -1.0), UiBase_Current, axis);
    break;
  case UiAlign_MiddleLeft:
    ui_cmd_push_rect_pos(cmdBuffer, UiBase_Current, ui_vector(0.0, -0.5), UiBase_Current, axis);
    break;
  case UiAlign_MiddleCenter:
    ui_cmd_push_rect_pos(cmdBuffer, UiBase_Current, ui_vector(-0.5, -0.5), UiBase_Current, axis);
    break;
  case UiAlign_MiddleRight:
    ui_cmd_push_rect_pos(cmdBuffer, UiBase_Current, ui_vector(-1.0, -0.5), UiBase_Current, axis);
    break;
  case UiAlign_BottomLeft:
    break;
  case UiAlign_BottomCenter:
    ui_cmd_push_rect_pos(cmdBuffer, UiBase_Current, ui_vector(-0.5, 0.0), UiBase_Current, axis);
    break;
  case UiAlign_BottomRight:
    ui_cmd_push_rect_pos(cmdBuffer, UiBase_Current, ui_vector(-1.0, 0.0), UiBase_Current, axis);
    break;
  }
}

void ui_layout_resize_to(
    UiCanvasComp* canvas, const UiBase base, const UiAlign align, const UiAxis axis) {

  UiCmdBuffer* cmdBuffer = ui_canvas_cmd_buffer(canvas);
  switch (align) {
  case UiAlign_TopLeft:
    ui_cmd_push_rect_size_to(cmdBuffer, base, ui_vector(0.0, 1.0), base, axis);
    break;
  case UiAlign_TopCenter:
    ui_cmd_push_rect_size_to(cmdBuffer, base, ui_vector(0.5, 1.0), base, axis);
    break;
  case UiAlign_TopRight:
    ui_cmd_push_rect_size_to(cmdBuffer, base, ui_vector(1.0, 1.0), base, axis);
    break;
  case UiAlign_MiddleLeft:
    ui_cmd_push_rect_size_to(cmdBuffer, base, ui_vector(0.0, 0.5), base, axis);
    break;
  case UiAlign_MiddleCenter:
    ui_cmd_push_rect_size_to(cmdBuffer, base, ui_vector(0.5, 0.5), base, axis);
    break;
  case UiAlign_MiddleRight:
    ui_cmd_push_rect_size_to(cmdBuffer, base, ui_vector(1.0, 0.5), base, axis);
    break;
  case UiAlign_BottomLeft:
    ui_cmd_push_rect_size_to(cmdBuffer, base, ui_vector(0.0, 0.0), base, axis);
    break;
  case UiAlign_BottomCenter:
    ui_cmd_push_rect_size_to(cmdBuffer, base, ui_vector(0.5, 0.0), base, axis);
    break;
  case UiAlign_BottomRight:
    ui_cmd_push_rect_size_to(cmdBuffer, base, ui_vector(1.0, 0.0), base, axis);
    break;
  }
}

void ui_layout_set(UiCanvasComp* canvas, const UiRect rect, const UiBase base) {
  UiCmdBuffer* cmdBuffer = ui_canvas_cmd_buffer(canvas);
  ui_cmd_push_rect_pos(cmdBuffer, base, rect.pos, base, Ui_XY);

  diag_assert_msg(rect.size.x >= 0.0f && rect.size.y >= 0.0f, "Negative sizes are not supported");
  ui_cmd_push_rect_size(cmdBuffer, rect.size, base, Ui_XY);
}

void ui_layout_set_pos(
    UiCanvasComp* canvas, const UiBase parent, const UiVector pos, const UiBase units) {
  UiCmdBuffer* cmdBuffer = ui_canvas_cmd_buffer(canvas);
  ui_cmd_push_rect_pos(cmdBuffer, parent, pos, units, Ui_XY);
}

void ui_layout_inner(
    UiCanvasComp*  canvas,
    const UiBase   parent,
    const UiAlign  align,
    const UiVector size,
    const UiBase   units) {
  ui_layout_move_to(canvas, parent, align, Ui_XY);
  ui_layout_resize(canvas, align, size, units, Ui_XY);
}
