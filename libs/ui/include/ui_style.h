#pragma once
#include "ui.h"

/**
 * Certain variations that have special meaning by convention.
 */
enum {
  UiVariation_Normal    = 0,
  UiVariation_Monospace = 1,
};

/**
 * Push / Pop an element to / from the style stack.
 * Useful for local changes to the current style with an easy way to restore the previous.
 */
void ui_style_push(UiCanvasComp*);
void ui_style_pop(UiCanvasComp*);

/**
 * Update the current style.
 */
void ui_style_color(UiCanvasComp*, UiColor);
void ui_style_color_with_mult(UiCanvasComp*, UiColor, f32 mult);
void ui_style_color_mult(UiCanvasComp*, f32 mult);
void ui_style_outline(UiCanvasComp*, u8 outline);
void ui_style_layer(UiCanvasComp*, UiLayer);
void ui_style_mode(UiCanvasComp*, UiMode);
void ui_style_variation(UiCanvasComp*, u8 variation);
void ui_style_weight(UiCanvasComp*, UiWeight);
