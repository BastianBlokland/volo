#include "core_alloc.h"
#include "core_array.h"
#include "core_float.h"
#include "core_format.h"
#include "core_math.h"
#include "core_path.h"
#include "debug_sound.h"
#include "ecs_view.h"
#include "ecs_world.h"
#include "snd_mixer.h"
#include "ui.h"

static const String g_tooltipMixerGain = string_static("Mixer output gain.");

typedef enum {
  DebugSoundTab_Mixer,
  DebugSoundTab_Objects,

  DebugSoundTab_Count,
} DebugSoundTab;

static const String g_soundTabNames[] = {
    string_static("\uE429 Mixer"),
    string_static("\uE574 Objects"),
};
ASSERT(array_elems(g_soundTabNames) == DebugSoundTab_Count, "Incorrect number of names");

ecs_comp_define(DebugSoundPanelComp) {
  UiPanel      panel;
  UiScrollview scrollview;
  DynString    nameFilter;
  u32          lastObjectRows;
};

ecs_view_define(GlobalView) { ecs_access_write(SndMixerComp); }

ecs_view_define(PanelUpdateView) {
  ecs_view_flags(EcsViewFlags_Exclusive); // DebugSoundPanelComp's are exclusively managed here.

  ecs_access_read(DebugPanelComp);
  ecs_access_write(DebugSoundPanelComp);
  ecs_access_write(UiCanvasComp);
}

static void ecs_destruct_sound_panel(void* data) {
  DebugSoundPanelComp* comp = data;
  dynstring_destroy(&comp->nameFilter);
}

static bool sound_panel_filter(DebugSoundPanelComp* panelComp, const String name) {
  if (string_is_empty(panelComp->nameFilter)) {
    return true;
  }
  const String rawFilter = dynstring_view(&panelComp->nameFilter);
  const String filter    = fmt_write_scratch("*{}*", fmt_text(rawFilter));
  return string_match_glob(name, filter, StringMatchFlags_IgnoreCase);
}

static void sound_draw_bg(UiCanvasComp* c) {
  ui_style_push(c);
  ui_style_color(c, ui_color(0, 0, 0, 64));
  ui_style_outline(c, 2);
  ui_canvas_draw_glyph(c, UiShape_Square, 0, UiFlags_None);
  ui_style_pop(c);
}

static void sound_draw_progress(UiCanvasComp* c, const f32 progress) {
  ui_style_push(c);

  ui_style_outline(c, 3);
  ui_style_color(c, ui_color(128, 128, 128, 178));
  ui_canvas_draw_glyph(c, UiShape_Square, 5, UiFlags_None);

  ui_style_outline(c, 0);
  ui_style_color(c, ui_color(0, 255, 0, 128));

  ui_layout_push(c);
  ui_layout_set(c, ui_rect(ui_vector(0, 0), ui_vector(progress, 1)), UiBase_Current);
  ui_canvas_draw_glyph(c, UiShape_Square, 5, UiFlags_None);
  ui_layout_pop(c);

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
static f32 sound_magnitude_to_db(const f32 magnitude) {
  static const f32 g_dbMin = -50.0f;
  if (magnitude < 0.00316f /* -50.0f db as magnitude */) {
    return g_dbMin;
  }
  return 20.0f * math_log10_f32(magnitude);
}

/**
 * Convert the given decibel value to a normalized fraction.
 */
static f32 sound_db_to_fraction(const f32 db) {
  static const f32 g_dbMin = -50.0f;
  return math_unlerp(g_dbMin, 0, db);
}

static UiColor sound_color_from_fraction(const f32 fraction) {
  const f32 warnFraction = 0.85f;
  if (fraction < 0) {
    return ui_color_lime;
  } else if (fraction < warnFraction) {
    return ui_color_lerp(ui_color_lime, ui_color_yellow, fraction / warnFraction);
  } else {
    const f32 factor = (math_min(fraction, 1.0f) - warnFraction) * (1.0f / (1.0f - warnFraction));
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

    ui_style_color(c, sound_color_from_fraction(sampleAbs));

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
  const f32    peakDb       = sound_magnitude_to_db(snd_buffer_magnitude_peak(buf, chan));
  const f32    peakFraction = sound_db_to_fraction(peakDb);
  const f32    rmsDb        = sound_magnitude_to_db(snd_buffer_magnitude_rms(buf, chan));
  const f32    rmsFraction  = sound_db_to_fraction(rmsDb);
  const String levelText    = fmt_write_scratch(
      "Level: \a|02\ab{}{<5}\ar Peak\n"
      "Level: \a|02\ab{}{<5}\ar  RMS",
      fmt_ui_color(sound_color_from_fraction(peakFraction)),
      fmt_float(peakDb, .plusSign = true, .minIntDigits = 2, .minDecDigits = 1, .maxDecDigits = 1),
      fmt_ui_color(sound_color_from_fraction(rmsFraction)),
      fmt_float(rmsDb, .plusSign = true, .minIntDigits = 2, .minDecDigits = 1, .maxDecDigits = 1));
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
  for (u32 i = 0; i != BucketCount; ++i) {
    const f32 fraction = sound_db_to_fraction(sound_magnitude_to_db(buckets[i]));
    if (fraction <= f32_epsilon) {
      continue;
    }
    const UiVector size = {.width = bucketStep, .height = math_clamp_f32(fraction, 0, 1)};
    const UiVector pos  = {.x = i / (f32)BucketCount, .y = 0};

    ui_style_color(c, sound_color_from_fraction(fraction));

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

static void sound_draw_mixer_stats(UiCanvasComp* c, SndMixerComp* m) {
  ui_layout_push(c);
  ui_layout_container_push(c, UiClip_None);

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 125);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  sound_draw_table_header(c, &table, string_lit("Device"));
  const String deviceText = fmt_write_scratch(
      "{} ({}) [{}] Underruns: {}",
      fmt_text(snd_mixer_device_id(m)),
      fmt_text(snd_mixer_device_backend(m)),
      fmt_text(snd_mixer_device_state(m)),
      fmt_int(snd_mixer_device_underruns(m)));
  ui_label(c, deviceText, .selectable = true);

  const u32 objectsPlaying   = snd_mixer_objects_playing(m);
  const u32 objectsAllocated = snd_mixer_objects_allocated(m);
  sound_draw_table_header(c, &table, string_lit("Objects"));
  const String objectsText = fmt_write_scratch(
      "Playing: {<4} Allocated: {}", fmt_int(objectsPlaying), fmt_int(objectsAllocated));
  ui_label(c, objectsText);

  ui_layout_container_pop(c);
  ui_layout_pop(c);
}

static void sound_draw_mixer_controls(UiCanvasComp* c, SndMixerComp* m) {
  ui_layout_push(c);
  ui_layout_container_push(c, UiClip_Rect);

  UiTable table = ui_table();
  ui_table_add_column(&table, UiTableColumn_Fixed, 125);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  sound_draw_table_header(c, &table, string_lit("Gain"));
  f32 gain = snd_mixer_gain_get(m);
  if (ui_slider(c, &gain, .max = 2.0f, .tooltip = g_tooltipMixerGain)) {
    snd_mixer_gain_set(m, gain);
  }

  sound_draw_table_header(c, &table, string_lit("Limiter"));
  ui_style_push(c);
  const f32 limiter = snd_mixer_limiter_get(m);
  if (limiter < 1.0) {
    ui_style_color(c, ui_color_lime);
  }
  ui_label(c, fmt_write_scratch("{}", fmt_float(limiter, .minDecDigits = 2, .maxDecDigits = 2)));
  ui_style_pop(c);

  ui_layout_container_pop(c);
  ui_layout_pop(c);
}

static void sound_mixer_draw(UiCanvasComp* c, SndMixerComp* m) {
  UiTable table = ui_table(.rowHeight = 100);
  ui_table_add_column(&table, UiTableColumn_Fixed, 80);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  sound_draw_table_header(c, &table, string_lit("Stats"));
  sound_draw_bg(c);
  sound_draw_mixer_stats(c, m);

  sound_draw_table_header(c, &table, string_lit("Controls"));
  sound_draw_bg(c);
  sound_draw_mixer_controls(c, m);

  const SndBufferView history = snd_mixer_history(m);
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
}

static void sound_objects_options_draw(UiCanvasComp* c, DebugSoundPanelComp* panelComp) {
  ui_layout_push(c);

  UiTable table = ui_table(.spacing = ui_vector(10, 5), .rowHeight = 20);
  ui_table_add_column(&table, UiTableColumn_Fixed, 60);
  ui_table_add_column(&table, UiTableColumn_Fixed, 300);

  ui_table_next_row(c, &table);
  ui_label(c, string_lit("Filter:"));
  ui_table_next_column(c, &table);
  ui_textbox(c, &panelComp->nameFilter, .placeholder = string_lit("*"));

  ui_layout_pop(c);
}

static void sound_objects_draw(UiCanvasComp* c, DebugSoundPanelComp* panelComp, SndMixerComp* m) {
  sound_objects_options_draw(c, panelComp);
  ui_layout_grow(c, UiAlign_BottomCenter, ui_vector(0, -35), UiBase_Absolute, Ui_Y);
  ui_layout_container_push(c, UiClip_None);

  UiTable table = ui_table(.spacing = ui_vector(10, 5));
  ui_table_add_column(&table, UiTableColumn_Fixed, 200);
  ui_table_add_column(&table, UiTableColumn_Fixed, 80);
  ui_table_add_column(&table, UiTableColumn_Fixed, 80);
  ui_table_add_column(&table, UiTableColumn_Fixed, 80);
  ui_table_add_column(&table, UiTableColumn_Fixed, 80);
  ui_table_add_column(&table, UiTableColumn_Flexible, 0);

  ui_table_draw_header(
      c,
      &table,
      (const UiTableColumnName[]){
          {string_lit("Name"), string_lit("Name of the sound-object.")},
          {string_lit("Rate"), string_lit("Rate of sound frames (in hertz).")},
          {string_lit("Channels"), string_lit("Amount of channels per frame.")},
          {string_lit("Pitch"), string_lit("Current pitch.")},
          {string_lit("Gain"), string_lit("Current gain (L/R).")},
          {string_lit("Progress"), string_lit("Current progress.")},
      });

  const u32 lastObjectRows  = panelComp->lastObjectRows;
  panelComp->lastObjectRows = 0;

  ui_scrollview_begin(c, &panelComp->scrollview, ui_table_height(&table, lastObjectRows));

  ui_canvas_id_block_next(c); // Start the list of objects on its own id block.
  for (SndObjectId obj = sentinel_u32; obj = snd_object_next(m, obj), !sentinel_check(obj);) {
    const String name = snd_object_get_name(m, obj);
    if (!sound_panel_filter(panelComp, name)) {
      continue;
    }
    const u32          frameCount    = snd_object_get_frame_count(m, obj);
    const u32          frameRate     = snd_object_get_frame_rate(m, obj);
    const u8           frameChannels = snd_object_get_frame_channels(m, obj);
    const f64          cursor        = snd_object_get_cursor(m, obj);
    const f32          progress      = frameCount ? (f32)(cursor / (f64)frameCount) : 0;
    const f32          pitch         = snd_object_get_pitch(m, obj);
    const f32          gainLeft      = snd_object_get_gain(m, obj, SndChannel_Left);
    const f32          gainRight     = snd_object_get_gain(m, obj, SndChannel_Right);
    const TimeDuration duration      = frameCount ? frameCount * time_second / frameRate : 0;
    const TimeDuration elapsed = frameCount ? (TimeDuration)(cursor * time_second / frameRate) : 0;

    ui_canvas_id_block_index(c, obj); // Set a stable canvas id.
    ui_table_next_row(c, &table);
    ui_table_draw_row_bg(c, &table, ui_color(48, 48, 48, 192));

    ui_label(c, path_stem(name), .selectable = true, .tooltip = name);
    ui_table_next_column(c, &table);

    ui_label(c, fmt_write_scratch("{}hz", fmt_int(frameRate)));
    ui_table_next_column(c, &table);

    ui_label(c, fmt_write_scratch("{}", fmt_int(frameChannels)));
    ui_table_next_column(c, &table);

    const String pitchText = fmt_write_scratch(
        "{}", fmt_float(pitch, .minDecDigits = 2, .maxDecDigits = 2, .expThresholdNeg = 0));
    ui_label(c, pitchText);
    ui_table_next_column(c, &table);

    const String gainText = fmt_write_scratch(
        "{} / {}",
        fmt_float(gainLeft, .minDecDigits = 1, .maxDecDigits = 1, .expThresholdNeg = 0),
        fmt_float(gainRight, .minDecDigits = 1, .maxDecDigits = 1, .expThresholdNeg = 0));
    ui_label(c, gainText);
    ui_table_next_column(c, &table);

    sound_draw_progress(c, progress);
    if (!snd_object_is_loading(m, obj)) {
      const f32    elapsedSecs  = elapsed / (f32)time_second;
      const f32    durationSecs = duration / (f32)time_second;
      const String progressText = fmt_write_scratch(
          "{}s / {}s",
          fmt_float(elapsedSecs, .minDecDigits = 1, .maxDecDigits = 1, .expThresholdNeg = 0),
          fmt_float(durationSecs, .minDecDigits = 1, .maxDecDigits = 1, .expThresholdNeg = 0));
      ui_style_push(c);
      ui_style_variation(c, UiVariation_Monospace);
      ui_style_outline(c, 2);
      ui_label(c, progressText, .align = UiAlign_MiddleCenter);
      ui_style_pop(c);
    }

    ++panelComp->lastObjectRows;
  }
  ui_canvas_id_block_next(c);

  ui_scrollview_end(c, &panelComp->scrollview);
  ui_layout_container_pop(c);
}

static void sound_panel_draw(UiCanvasComp* c, DebugSoundPanelComp* panelComp, SndMixerComp* m) {
  const String title = fmt_write_scratch("{} Sound Panel", fmt_ui_shape(MusicNote));
  ui_panel_begin(
      c,
      &panelComp->panel,
      .title       = title,
      .tabNames    = g_soundTabNames,
      .tabCount    = DebugSoundTab_Count,
      .topBarColor = ui_color(100, 0, 0, 192));

  switch (panelComp->panel.activeTab) {
  case DebugSoundTab_Mixer:
    sound_mixer_draw(c, m);
    break;
  case DebugSoundTab_Objects:
    sound_objects_draw(c, panelComp, m);
    break;
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
    const bool pinned = ui_panel_pinned(&panelComp->panel);
    if (debug_panel_hidden(ecs_view_read_t(itr, DebugPanelComp)) && !pinned) {
      continue;
    }
    sound_panel_draw(canvas, panelComp, mixer);

    if (ui_panel_closed(&panelComp->panel)) {
      ecs_world_entity_destroy(world, entity);
    }
    if (ui_canvas_status(canvas) >= UiStatus_Pressed) {
      ui_canvas_to_front(canvas);
    }
  }
}

ecs_module_init(debug_sound_module) {
  ecs_register_comp(DebugSoundPanelComp, .destructor = ecs_destruct_sound_panel);

  ecs_register_view(GlobalView);
  ecs_register_view(PanelUpdateView);

  ecs_register_system(
      DebugSoundUpdatePanelSys, ecs_view_id(GlobalView), ecs_view_id(PanelUpdateView));
}

EcsEntityId
debug_sound_panel_open(EcsWorld* world, const EcsEntityId window, const DebugPanelType type) {
  const EcsEntityId    panelEntity = debug_panel_create(world, window, type);
  DebugSoundPanelComp* soundPanel  = ecs_world_add_t(
      world,
      panelEntity,
      DebugSoundPanelComp,
      .panel      = ui_panel(.size = ui_vector(800, 685)),
      .scrollview = ui_scrollview(),
      .nameFilter = dynstring_create(g_allocHeap, 32));

  if (type == DebugPanelType_Detached) {
    ui_panel_maximize(&soundPanel->panel);
  }

  return panelEntity;
}
