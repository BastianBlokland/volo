#include "core_diag.h"
#include "ui_style.h"

#include "canvas_internal.h"

void ui_style_push(UiCanvasComp* canvas) {
  UiCmdBuffer* cmdBuffer = ui_canvas_cmd_buffer(canvas);
  ui_cmd_push_style_push(cmdBuffer);
}

void ui_style_pop(UiCanvasComp* canvas) {
  UiCmdBuffer* cmdBuffer = ui_canvas_cmd_buffer(canvas);
  ui_cmd_push_style_pop(cmdBuffer);
}

void ui_style_color(UiCanvasComp* canvas, const UiColor color) {
  UiCmdBuffer* cmdBuffer = ui_canvas_cmd_buffer(canvas);
  ui_cmd_push_style_color(cmdBuffer, color);
}

void ui_style_outline(UiCanvasComp* canvas, const u8 outline) {
  UiCmdBuffer* cmdBuffer = ui_canvas_cmd_buffer(canvas);
  ui_cmd_push_style_outline(cmdBuffer, outline);
}

void ui_style_layer(UiCanvasComp* canvas, const UiLayer layer) {
  UiCmdBuffer* cmdBuffer = ui_canvas_cmd_buffer(canvas);
  ui_cmd_push_style_layer(cmdBuffer, layer);
}
