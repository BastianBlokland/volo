#include "core_format.h"
#include "core_math.h"
#include "debug_sound.h"
#include "ecs_world.h"
#include "snd.h"
#include "ui.h"

ecs_comp_define(DebugSoundPanelComp) { UiPanel panel; };

ecs_view_define(GlobalView) { ecs_access_write(SndMixerComp); }

ecs_view_define(PanelUpdateView) {
  ecs_access_write(DebugSoundPanelComp);
  ecs_access_write(UiCanvasComp);
}

static void sound_draw_bg(UiCanvasComp* canvas) {
  ui_style_push(canvas);
  ui_style_color(canvas, ui_color(0, 0, 0, 64));
  ui_style_outline(canvas, 2);
  ui_canvas_draw_glyph(canvas, UiShape_Square, 0, UiFlags_None);
  ui_style_pop(canvas);
}

static void sound_draw_table_header(UiCanvasComp* canvas, UiTable* table, const String header) {
  ui_table_next_row(canvas, table);
  ui_label(canvas, header);
  ui_table_next_column(canvas, table);
}

static UiColor sound_level_color(const f32 level) {
  const f32 warnLevel = 0.85f;
  if (level < warnLevel) {
    return ui_color_lerp(ui_color_lime, ui_color_yellow, level / warnLevel);
  } else {
    const f32 factor = (math_min(level, 1.0f) - warnLevel) * (1.0f / (1.0f - warnLevel));
    return ui_color_lerp(ui_color_yellow, ui_color_red, factor);
  }
}

static void sound_draw_time(UiCanvasComp* canvas, const SndBufferView buf, const SndChannel chan) {
  static const f32 g_step = 1.0f / 256.0f;

  ui_style_push(canvas);
  ui_style_outline(canvas, 0);

  for (f32 t = 0.0; t < 1.0f; t += g_step) {
    const f32 sample    = snd_buffer_sample(buf, chan, t);
    const f32 sampleAbs = math_abs(sample);

    const UiVector size = {.width = g_step, .height = math_clamp_f32(sampleAbs, 0, 1) * 0.5f};
    const UiVector pos  = {.x = t, .y = sample > 0.0f ? 0.5f : 0.5f - size.height};

    ui_style_color(canvas, sound_level_color(sampleAbs));

    ui_layout_push(canvas);
    ui_layout_set(canvas, ui_rect(pos, size), UiBase_Current);
    ui_canvas_draw_glyph(canvas, UiShape_Square, 5, UiFlags_None);
    ui_layout_pop(canvas);
  }

  ui_style_pop(canvas);
}

static void sound_draw_stats(UiCanvasComp* canvas, const SndBufferView buf, const SndChannel chan) {
  ui_style_push(canvas);
  ui_style_variation(canvas, UiVariation_Monospace);

  ui_layout_push(canvas);
  ui_layout_grow(canvas, UiAlign_MiddleCenter, ui_vector(-10, -10), UiBase_Absolute, Ui_XY);

  const f32    levelRms  = snd_buffer_level_rms(buf, chan);
  const f32    levelPeak = snd_buffer_level_peak(buf, chan);
  const String text      = fmt_write_scratch(
      "Level: \ab{}{<6}\ar RMS\nLevel: \ab{}{<6}\ar Peak",
      fmt_ui_color(sound_level_color(levelRms)),
      fmt_float(levelRms, .minDecDigits = 4, .maxDecDigits = 4),
      fmt_ui_color(sound_level_color(levelPeak)),
      fmt_float(levelPeak, .minDecDigits = 4, .maxDecDigits = 4));

  ui_label(canvas, text, .align = UiAlign_TopLeft);
  ui_layout_pop(canvas);
  ui_style_pop(canvas);
}

static void sound_draw_device_info(UiCanvasComp* canvas, SndMixerComp* mixer) {
  ui_layout_push(canvas);
  ui_layout_container_push(canvas, UiClip_None);

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  sound_draw_table_header(canvas, &table, string_lit("Id"));
  ui_label(canvas, snd_mixer_device_id(mixer), .selectable = true);

  sound_draw_table_header(canvas, &table, string_lit("State"));
  ui_label(canvas, snd_mixer_device_state(mixer));

  sound_draw_table_header(canvas, &table, string_lit("Underruns"));
  ui_label(canvas, fmt_write_scratch("{}", fmt_int(snd_mixer_device_underruns(mixer))));

  ui_layout_container_pop(canvas);
  ui_layout_pop(canvas);
}

static void sound_draw_controls(UiCanvasComp* canvas, SndMixerComp* mixer) {
  ui_layout_push(canvas);
  ui_layout_container_push(canvas, UiClip_None);

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  sound_draw_table_header(canvas, &table, string_lit("Gain"));
  f32 gain = snd_mixer_gain_get(mixer);
  if (ui_slider(canvas, &gain)) {
    snd_mixer_gain_set(mixer, gain);
  }

  ui_layout_container_pop(canvas);
  ui_layout_pop(canvas);
}

static void
sound_panel_draw(UiCanvasComp* canvas, DebugSoundPanelComp* panelComp, SndMixerComp* mixer) {

  const String title = fmt_write_scratch("{} Sound Panel", fmt_ui_shape(MusicNote));
  ui_panel_begin(canvas, &panelComp->panel, .title = title);

  UiTable table = ui_table(.rowHeight = 100);
  ui_table_add_column(&table, UiTableColumn_Fixed, 125);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  sound_draw_table_header(canvas, &table, string_lit("Device"));
  sound_draw_bg(canvas);
  sound_draw_device_info(canvas, mixer);

  sound_draw_table_header(canvas, &table, string_lit("Controls"));
  sound_draw_bg(canvas);
  sound_draw_controls(canvas, mixer);

  const SndBufferView history = snd_mixer_history(mixer);
  for (SndChannel chan = 0; chan != SndChannel_Count; ++chan) {
    const String header = fmt_write_scratch("Channel {}", fmt_text(snd_channel_str(chan)));
    sound_draw_table_header(canvas, &table, header);
    sound_draw_bg(canvas);
    sound_draw_time(canvas, history, chan);
    sound_draw_stats(canvas, history, chan);
  }

  ui_panel_end(canvas, &panelComp->panel);
}

ecs_system_define(DebugSoundUpdatePanelSys) {
  EcsView*     globalView = ecs_world_view_t(world, GlobalView);
  EcsIterator* globalItr  = ecs_view_maybe_at(globalView, ecs_world_global(world));
  if (!globalItr) {
    return;
  }
  SndMixerComp* mixer = ecs_view_write_t(globalItr, SndMixerComp);

  EcsView* panelView = ecs_world_view_t(world, PanelUpdateView);
  for (EcsIterator* itr = ecs_view_itr(panelView); ecs_view_walk(itr);) {
    const EcsEntityId    entity    = ecs_view_entity(itr);
    DebugSoundPanelComp* panelComp = ecs_view_write_t(itr, DebugSoundPanelComp);
    UiCanvasComp*        canvas    = ecs_view_write_t(itr, UiCanvasComp);

    ui_canvas_reset(canvas);
    sound_panel_draw(canvas, panelComp, mixer);

    if (panelComp->panel.flags & UiPanelFlags_Close) {
      ecs_world_entity_destroy(world, entity);
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_sound_module) {
  ecs_register_comp(DebugSoundPanelComp);

  ecs_register_view(GlobalView);
  ecs_register_view(PanelUpdateView);

  ecs_register_system(
      DebugSoundUpdatePanelSys, ecs_view_id(GlobalView), ecs_view_id(PanelUpdateView));
}

EcsEntityId debug_sound_panel_open(EcsWorld* world, const EcsEntityId window) {
  const EcsEntityId panelEntity = ui_canvas_create(world, window, UiCanvasCreateFlags_ToFront);
  ecs_world_add_t(
      world, panelEntity, DebugSoundPanelComp, .panel = ui_panel(.size = ui_vector(750, 500)));
  return panelEntity;
}
