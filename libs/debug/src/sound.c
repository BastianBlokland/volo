#include "core_diag.h"
#include "core_float.h"
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

static void sound_draw_bg(UiCanvasComp* c) {
  ui_style_push(c);
  ui_style_color(c, ui_color(0, 0, 0, 64));
  ui_style_outline(c, 2);
  ui_canvas_draw_glyph(c, UiShape_Square, 0, UiFlags_None);
  ui_style_pop(c);
}

static void sound_draw_table_header(UiCanvasComp* c, UiTable* table, const String header) {
  ui_table_next_row(c, table);
  ui_label(c, header);
  ui_table_next_column(c, table);
}

/**
 * Convert the given magnitude to decibel (logarithmic scale).
 * Ouput range: <0: attenuated, 0: maximum ouput without clipping, >0: boosted.
 */
static f32 sound_magnitude_to_db(const f32 magnitude) { return 20 * math_log10_f32(magnitude); }

static UiColor sound_level_color(const f32 level) {
  const f32 warnLevel = 0.85f;
  if (level < warnLevel) {
    return ui_color_lerp(ui_color_lime, ui_color_yellow, level / warnLevel);
  } else {
    const f32 factor = (math_min(level, 1.0f) - warnLevel) * (1.0f / (1.0f - warnLevel));
    return ui_color_lerp(ui_color_yellow, ui_color_red, factor);
  }
}

static void sound_draw_time(UiCanvasComp* c, const SndBufferView buf, const SndChannel chan) {
  static const f32 g_step = 1.0f / 256.0f;

  ui_style_push(c);
  ui_style_outline(c, 0);

  for (f32 t = 0.0; t < 1.0f; t += g_step) {
    const f32 sample    = snd_buffer_sample(buf, chan, t);
    const f32 sampleAbs = math_abs(sample);

    const UiVector size = {.width = g_step, .height = math_clamp_f32(sampleAbs, 0, 1) * 0.5f};
    const UiVector pos  = {.x = t, .y = sample > 0.0f ? 0.5f : 0.5f - size.height};

    ui_style_color(c, sound_level_color(sampleAbs));

    ui_layout_push(c);
    ui_layout_set(c, ui_rect(pos, size), UiBase_Current);
    ui_canvas_draw_glyph(c, UiShape_Square, 5, UiFlags_None);
    ui_layout_pop(c);
  }

  ui_style_pop(c);
}

static void sound_draw_time_stats(UiCanvasComp* c, const SndBufferView buf, const SndChannel chan) {
  ui_style_push(c);
  ui_style_variation(c, UiVariation_Monospace);

  ui_layout_push(c);
  ui_layout_grow(c, UiAlign_MiddleCenter, ui_vector(-10, -10), UiBase_Absolute, Ui_XY);

  // Name label.
  ui_label(c, string_lit("Time domain"), .align = UiAlign_TopLeft);

  // Y-axis label.
  const TimeDuration duration = snd_buffer_duration(buf);
  ui_label(c, string_lit("0ms"), .align = UiAlign_BottomLeft);
  ui_label(c, fmt_write_scratch("{}", fmt_duration(duration)), .align = UiAlign_BottomRight);
  ui_label(c, fmt_write_scratch("{}", fmt_duration(duration / 2)), .align = UiAlign_BottomCenter);

  // Signal level labels.
  const f32    levelRms  = snd_buffer_magnitude_rms(buf, chan);
  const f32    levelPeak = snd_buffer_magnitude_peak(buf, chan);
  const String levelText = fmt_write_scratch(
      "Level: \a|02\ab{}{<6}\ar  RMS\n"
      "Level: \a|02\ab{}{<6}\ar Peak",
      fmt_ui_color(sound_level_color(levelRms)),
      fmt_float(levelRms, .minDecDigits = 4, .maxDecDigits = 4),
      fmt_ui_color(sound_level_color(levelPeak)),
      fmt_float(levelPeak, .minDecDigits = 4, .maxDecDigits = 4));
  ui_label(c, levelText, .align = UiAlign_TopRight);

  ui_layout_pop(c);
  ui_style_pop(c);
}

static void sound_draw_spectrum(UiCanvasComp* c, const SndBufferView buf, const SndChannel chan) {
  enum { BucketCount = 256, SliceSampleCount = BucketCount * 2 };

  const u32 sliceCount = buf.frameCount / SliceSampleCount;

  f32 buckets[BucketCount] = {0};
  for (u32 sliceIdx = 0; sliceIdx != sliceCount; ++sliceIdx) {
    // Compute the frequency spectrum of the slice.
    const u32           sliceOffset = sliceIdx * SliceSampleCount;
    const SndBufferView slice       = snd_buffer_slice(buf, sliceOffset, SliceSampleCount);
    f32                 sliceBuckets[BucketCount];
    snd_buffer_spectrum(slice, chan, sliceBuckets);

    // Add it to the output buckets.
    for (u32 i = 0; i != BucketCount; ++i) {
      buckets[i] += sliceBuckets[i];
    }
  }

  // Normalize the buckets.
  const f32 normFactor = sliceCount ? 1.0f / sliceCount : 1.0f;
  for (u32 i = 0; i != BucketCount; ++i) {
    buckets[i] *= normFactor;
  }

  ui_style_push(c);
  ui_style_outline(c, 0);

  const f32 bucketStep = 1.0f / (f32)BucketCount;
  const f32 dbMin      = -50.0f;
  for (u32 i = 0; i != BucketCount; ++i) {
    const f32 value = math_unlerp(dbMin, 0, sound_magnitude_to_db(buckets[i]));
    if (value <= f32_epsilon) {
      continue;
    }
    const UiVector size = {.width = bucketStep, .height = math_clamp_f32(value, 0, 1)};
    const UiVector pos  = {.x = i / (f32)BucketCount, .y = 0};

    ui_style_color(c, sound_level_color(value));

    ui_layout_push(c);
    ui_layout_set(c, ui_rect(pos, size), UiBase_Current);
    ui_canvas_draw_glyph(c, UiShape_Square, 5, UiFlags_None);
    ui_layout_pop(c);
  }

  ui_style_pop(c);
}

static void sound_draw_spectrum_stats(UiCanvasComp* c, const SndBufferView buf) {
  ui_style_push(c);
  ui_style_variation(c, UiVariation_Monospace);

  ui_layout_push(c);
  ui_layout_grow(c, UiAlign_MiddleCenter, ui_vector(-10, -10), UiBase_Absolute, Ui_XY);

  // Name label.
  ui_label(c, string_lit("Frequency domain"), .align = UiAlign_TopLeft);

  // Y-axis label.
  const f32 freqMax = snd_buffer_frequency_max(buf);
  ui_label(c, string_lit("0hz"), .align = UiAlign_BottomLeft);
  ui_label(c, fmt_write_scratch("{}hz", fmt_float(freqMax)), .align = UiAlign_BottomRight);
  ui_label(c, fmt_write_scratch("{}hz", fmt_float(freqMax * 0.5f)), .align = UiAlign_BottomCenter);

  ui_layout_pop(c);
  ui_style_pop(c);
}

static void sound_draw_device_info(UiCanvasComp* c, SndMixerComp* mixer) {
  ui_layout_push(c);
  ui_layout_container_push(c, UiClip_None);

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  sound_draw_table_header(c, &table, string_lit("Id"));
  ui_label(c, snd_mixer_device_id(mixer), .selectable = true);

  sound_draw_table_header(c, &table, string_lit("State"));
  ui_label(c, snd_mixer_device_state(mixer));

  sound_draw_table_header(c, &table, string_lit("Underruns"));
  ui_label(c, fmt_write_scratch("{}", fmt_int(snd_mixer_device_underruns(mixer))));

  ui_layout_container_pop(c);
  ui_layout_pop(c);
}

static void sound_draw_controls(UiCanvasComp* c, SndMixerComp* mixer) {
  ui_layout_push(c);
  ui_layout_container_push(c, UiClip_None);

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 100);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  sound_draw_table_header(c, &table, string_lit("Gain"));
  f32 gain = snd_mixer_gain_get(mixer);
  if (ui_slider(c, &gain)) {
    snd_mixer_gain_set(mixer, gain);
  }

  ui_layout_container_pop(c);
  ui_layout_pop(c);
}

static void sound_panel_draw(UiCanvasComp* c, DebugSoundPanelComp* panelComp, SndMixerComp* mixer) {
  const String title = fmt_write_scratch("{} Sound Panel", fmt_ui_shape(MusicNote));
  ui_panel_begin(c, &panelComp->panel, .title = title);

  UiTable table = ui_table(.rowHeight = 100);
  ui_table_add_column(&table, UiTableColumn_Fixed, 125);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  sound_draw_table_header(c, &table, string_lit("Device"));
  sound_draw_bg(c);
  sound_draw_device_info(c, mixer);

  sound_draw_table_header(c, &table, string_lit("Controls"));
  sound_draw_bg(c);
  sound_draw_controls(c, mixer);

  const SndBufferView history = snd_mixer_history(mixer);
  for (SndChannel chan = 0; chan != SndChannel_Count; ++chan) {
    const String header = fmt_write_scratch("Channel {}", fmt_text(snd_channel_str(chan)));
    sound_draw_table_header(c, &table, header);

    // Time domain graph.
    sound_draw_bg(c);
    sound_draw_time(c, history, chan);
    sound_draw_time_stats(c, history, chan);

    ui_table_next_row(c, &table);
    ui_table_next_column(c, &table);

    // Frequency domain graph.
    sound_draw_bg(c);
    sound_draw_spectrum(c, history, chan);
    sound_draw_spectrum_stats(c, history);
  }

  ui_panel_end(c, &panelComp->panel);
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
      world, panelEntity, DebugSoundPanelComp, .panel = ui_panel(.size = ui_vector(750, 655)));
  return panelEntity;
}
