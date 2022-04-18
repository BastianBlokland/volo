#pragma once
#include "core_alloc.h"
#include "gap_window.h"

#include "builder_internal.h"

/**
 * Text Editor.
 *
 * Simple line editor operating on utf8 text, only supporting a single line at the moment.
 * NOTE: Does not support editing text with escape sequences (they are stripped out).
 */

typedef struct sUiEditor UiEditor;

UiEditor* ui_editor_create(Allocator*);
void      ui_editor_destroy(UiEditor*);

bool   ui_editor_active(const UiEditor*);      // Is the editor currently active.
UiId   ui_editor_element(const UiEditor*);     // Currently editing element.
String ui_editor_result_text(const UiEditor*); // Current result text.
String ui_editor_visual_text(const UiEditor*); // String to render while editing.

void ui_editor_start(UiEditor*, String initialText, UiId element);
void ui_editor_update(UiEditor*, GapWindowComp*, UiBuildHover, UiBuildTextInfo);
void ui_editor_stop(UiEditor*);
