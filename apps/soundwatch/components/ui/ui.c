/****************************************************************************
 * apps/soundwatch/components/ui/ui.c
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <syslog.h>
#include <lvgl/lvgl.h>

#include "soundwatch.h"
#include <fcntl.h>
extern void test_button_cb(lv_event_t *e);
#include <fcntl.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define UI_REFRESH_RATE_MS     100
#define UI_ALERT_TIMEOUT_MS    5000
#define UI_STATUS_BAR_HEIGHT   30
#define UI_EVENT_LOG_MAX       10

/* Colors */

#define UI_COLOR_BG            lv_color_hex(0x000000)
#define UI_COLOR_TEXT          lv_color_hex(0xFFFFFF)
#define UI_COLOR_ALERT_LOW     lv_color_hex(0x00FF00)
#define UI_COLOR_ALERT_MEDIUM  lv_color_hex(0xFFFF00)
#define UI_COLOR_ALERT_HIGH    lv_color_hex(0xFF8000)
#define UI_COLOR_ALERT_CRITICAL lv_color_hex(0xFF0000)
#define UI_COLOR_UNKNOWN       lv_color_hex(0x808080)

/* Sound type icons (placeholder characters) */

#define ICON_FIRE_ALARM        "\xEF\x89\xAF"  /* Fire icon */
#define ICON_CAR_HORN          "\xEF\x86\xB9"  /* Car icon */
#define ICON_DOORBELL          "\xEF\x83\xB0"  /* Bell icon */
#define ICON_HUMAN_CALL        "\xEF\x83\x8B"  /* User icon */
#define ICON_UNKNOWN           "\xEF\x84\xA9"  /* Question icon */

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* UI event log entry */

typedef struct ui_event_log_s
{
  sound_event_t event;
  uint32_t      display_time;
} ui_event_log_t;

/* UI context */

struct ui_ctx_s
{
  lv_obj_t *scr;              /* Main screen */
  lv_obj_t *status_bar;       /* Status bar */
  lv_obj_t *alert_panel;      /* Alert display panel */
  lv_obj_t *alert_icon;       /* Alert icon label */
  lv_obj_t *alert_text;       /* Alert text label */
  lv_obj_t *confidence_bar;   /* Confidence progress bar */
  lv_obj_t *event_log;        /* Event log container */
  lv_obj_t *status_label;     /* Status text label */

  lv_style_t style_bg;        /* Background style */
  lv_style_t style_text;      /* Text style */
  lv_style_t style_alert;     /* Alert style */

  lv_timer_t *refresh_timer;  /* Refresh timer */

  bool alert_visible;         /* Alert panel visibility */
  uint32_t alert_start_time;  /* Alert display start time */

  ui_event_log_t event_log_buffer[UI_EVENT_LOG_MAX];
  int event_log_count;
  int event_log_index;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static void ui_create_styles(struct ui_ctx_s *ctx);
static void ui_create_status_bar(struct ui_ctx_s *ctx);
static void ui_create_alert_panel(struct ui_ctx_s *ctx);
static void ui_create_test_button(struct ui_ctx_s *ctx)
{
  lv_obj_t *btn = lv_btn_create(ctx->scr);
  lv_obj_set_size(btn, 80, 30);
  lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 30);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0x222222), 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(btn, 8, 0);
  lv_obj_set_style_border_width(btn, 2, 0);
  lv_obj_set_style_border_color(btn, lv_color_hex(0x4488FF), 0);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_EVENT_BUBBLE);
  lv_obj_add_event_cb(btn, test_button_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_set_style_shadow_width(btn, 0, 0);

  lv_obj_t *label = lv_label_create(btn);
  lv_label_set_text(label, "TEST");
  lv_obj_set_style_text_color(label, lv_color_hex(0x4488FF), 0);
  lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
  lv_obj_center(label);
}

static void ui_create_event_log(struct ui_ctx_s *ctx);
static void ui_update_event_log_display(struct ui_ctx_s *ctx);
static void ui_refresh_timer_cb(lv_timer_t *timer);
static lv_color_t ui_get_alert_color(uint8_t alert_level);
static const char *ui_get_sound_icon(uint8_t sound_type);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ui_create_styles
 *
 * Description:
 *   Create UI styles.
 *
 ****************************************************************************/

static void ui_create_styles(struct ui_ctx_s *ctx)
{
  /* Background style */

  lv_style_init(&ctx->style_bg);
  lv_style_set_bg_color(&ctx->style_bg, UI_COLOR_BG);
  lv_style_set_bg_opa(&ctx->style_bg, LV_OPA_COVER);
  lv_style_set_border_width(&ctx->style_bg, 0);
  lv_style_set_pad_all(&ctx->style_bg, 0);

  /* Text style */

  lv_style_init(&ctx->style_text);
  lv_style_set_text_color(&ctx->style_text, UI_COLOR_TEXT);
  lv_style_set_text_font(&ctx->style_text, &lv_font_montserrat_14);

  /* Alert style */

  lv_style_init(&ctx->style_alert);
  lv_style_set_bg_opa(&ctx->style_alert, LV_OPA_80);
  lv_style_set_radius(&ctx->style_alert, 10);
  lv_style_set_pad_all(&ctx->style_alert, 10);
}

/****************************************************************************
 * Name: ui_create_status_bar
 *
 * Description:
 *   Create the status bar.
 *
 ****************************************************************************/

static void ui_create_status_bar(struct ui_ctx_s *ctx)
{
  /* Create status bar container */

  ctx->status_bar = lv_obj_create(ctx->scr);
  lv_obj_set_size(ctx->status_bar, LV_PCT(100), UI_STATUS_BAR_HEIGHT);
  lv_obj_align(ctx->status_bar, LV_ALIGN_TOP_MID, 0, 8);
  lv_obj_add_style(ctx->status_bar, &ctx->style_bg, 0);
  lv_obj_set_style_border_width(ctx->status_bar, 1, 0);
  lv_obj_set_style_border_color(ctx->status_bar, UI_COLOR_TEXT, 0);

  /* Create status label */

  ctx->status_label = lv_label_create(ctx->status_bar);
  lv_obj_add_style(ctx->status_label, &ctx->style_text, 0);
  lv_label_set_text(ctx->status_label, "SoundWatch - Listening...");
  lv_obj_center(ctx->status_label);
}

/****************************************************************************
 * Name: ui_create_alert_panel
 *
 * Description:
 *   Create the alert display panel.
 *
 ****************************************************************************/

static void ui_create_alert_panel(struct ui_ctx_s *ctx)
{
  /* Create alert panel container */

  ctx->alert_panel = lv_obj_create(ctx->scr);
  lv_obj_set_size(ctx->alert_panel, LV_PCT(90), LV_PCT(50));
  lv_obj_align(ctx->alert_panel, LV_ALIGN_CENTER, 0, -20);
  lv_obj_add_style(ctx->alert_panel, &ctx->style_alert, 0);
  lv_obj_set_style_bg_color(ctx->alert_panel, UI_COLOR_ALERT_LOW, 0);
  lv_obj_set_style_bg_opa(ctx->alert_panel, LV_OPA_0, 0);
  lv_obj_clear_flag(ctx->alert_panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(ctx->alert_panel, LV_OBJ_FLAG_HIDDEN);

  /* Create icon label */

  ctx->alert_icon = lv_label_create(ctx->alert_panel);
  lv_obj_set_style_text_font(ctx->alert_icon, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(ctx->alert_icon, UI_COLOR_TEXT, 0);
  lv_label_set_text(ctx->alert_icon, ICON_UNKNOWN);
  lv_obj_align(ctx->alert_icon, LV_ALIGN_CENTER, 0, -30);

  /* Create text label */

  ctx->alert_text = lv_label_create(ctx->alert_panel);
  lv_obj_add_style(ctx->alert_text, &ctx->style_text, 0);
  lv_label_set_text(ctx->alert_text, "Unknown Sound");
  lv_obj_align(ctx->alert_text, LV_ALIGN_CENTER, 0, 20);

  /* Create confidence progress bar */

  ctx->confidence_bar = lv_bar_create(ctx->alert_panel);
  lv_obj_set_size(ctx->confidence_bar, LV_PCT(80), 15);
  lv_obj_align(ctx->confidence_bar, LV_ALIGN_CENTER, 0, 50);
  lv_bar_set_range(ctx->confidence_bar, 0, 100);
  lv_bar_set_value(ctx->confidence_bar, 0, LV_ANIM_ON);
  lv_obj_set_style_bg_color(ctx->confidence_bar, lv_color_hex(0x333333), 0);
  lv_obj_set_style_bg_color(ctx->confidence_bar, UI_COLOR_ALERT_LOW,
                            LV_PART_INDICATOR);

  ctx->alert_visible = false;
  ctx->alert_start_time = 0;
}

/****************************************************************************
 * Name: ui_create_event_log
 *
 * Description:
 *   Create the event log container.
 *
 ****************************************************************************/

static void ui_create_event_log(struct ui_ctx_s *ctx)
{
  /* Create event log container */

  ctx->event_log = lv_obj_create(ctx->scr);
  lv_obj_set_size(ctx->event_log, LV_PCT(90), LV_PCT(30));
  lv_obj_align(ctx->event_log, LV_ALIGN_BOTTOM_MID, 0, -10);
  lv_obj_add_style(ctx->event_log, &ctx->style_bg, 0);
  lv_obj_set_style_border_width(ctx->event_log, 1, 0);
  lv_obj_set_style_border_color(ctx->event_log, UI_COLOR_TEXT, 0);
  lv_obj_set_flex_flow(ctx->event_log, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(ctx->event_log, 5, 0);

  /* Initialize event log */

  ctx->event_log_count = 0;
  ctx->event_log_index = 0;
  memset(ctx->event_log_buffer, 0, sizeof(ctx->event_log_buffer));
}

/****************************************************************************
 * Name: ui_refresh_timer_cb
 *
 * Description:
 *   Refresh timer callback.
 *
 ****************************************************************************/

static void ui_refresh_timer_cb(lv_timer_t *timer)
{
  struct ui_ctx_s *ctx = (struct ui_ctx_s *)lv_timer_get_user_data(timer);

  DEBUGASSERT(ctx != NULL);

  /* Check if alert should be hidden */

  if (ctx->alert_visible)
    {
      uint32_t elapsed = lv_tick_elaps(ctx->alert_start_time);

      if (elapsed >= UI_ALERT_TIMEOUT_MS)
        {
          lv_obj_add_flag(ctx->alert_panel, LV_OBJ_FLAG_HIDDEN);
          ctx->alert_visible = false;
        }
    }
}

/****************************************************************************
 * Name: ui_get_alert_color
 *
 * Description:
 *   Get the color for an alert level.
 *
 ****************************************************************************/

static lv_color_t ui_get_alert_color(uint8_t alert_level)
{
  switch (alert_level)
    {
      case ALERT_LEVEL_LOW:
        return UI_COLOR_ALERT_LOW;

      case ALERT_LEVEL_MEDIUM:
        return UI_COLOR_ALERT_MEDIUM;

      case ALERT_LEVEL_HIGH:
        return UI_COLOR_ALERT_HIGH;

      case ALERT_LEVEL_CRITICAL:
        return UI_COLOR_ALERT_CRITICAL;

      default:
        return UI_COLOR_UNKNOWN;
    }
}

/****************************************************************************
 * Name: ui_get_sound_icon
 *
 * Description:
 *   Get the icon for a sound type.
 *
 ****************************************************************************/

static const char *ui_get_sound_icon(uint8_t sound_type)
{
  switch (sound_type)
    {
      case SOUND_TYPE_FIRE_ALARM:
        return ICON_FIRE_ALARM;

      case SOUND_TYPE_CAR_HORN:
        return ICON_CAR_HORN;

      case SOUND_TYPE_DOORBELL:
        return ICON_DOORBELL;

      case SOUND_TYPE_HUMAN_CALL:
        return ICON_HUMAN_CALL;

      default:
        return ICON_UNKNOWN;
    }
}

/****************************************************************************
 * Name: ui_update_event_log_display
 *
 * Description:
 *   Update the event log display with current events.
 *
 ****************************************************************************/

static void ui_update_event_log_display(struct ui_ctx_s *ctx)
{
  lv_obj_t *child;
  char text[128];
  int i;
  int idx;
  lv_color_t color;

  DEBUGASSERT(ctx != NULL);

  /* Clear existing event log children */

  while (lv_obj_get_child_cnt(ctx->event_log) > 0)
    {
      child = lv_obj_get_child(ctx->event_log, 0);
      lv_obj_del(child);
    }

  /* Add event log entries */

  for (i = 0; i < ctx->event_log_count; i++)
    {
      /* Calculate index in ring buffer (oldest first) */

      idx = (ctx->event_log_index - ctx->event_log_count + i +
             UI_EVENT_LOG_MAX) % UI_EVENT_LOG_MAX;

      /* Get alert color */

      color = ui_get_alert_color(
        ctx->event_log_buffer[idx].event.alert_level);

      /* Create event entry label */

      child = lv_label_create(ctx->event_log);
      lv_obj_set_style_text_color(child, UI_COLOR_TEXT, 0);
      lv_obj_set_style_text_font(child, &lv_font_montserrat_14, 0);
      lv_obj_set_style_pad_ver(child, 2, 0);

      /* Format event text */

      snprintf(text, sizeof(text), "%s %s (%d%%)",
               ui_get_sound_icon(ctx->event_log_buffer[idx].event.type),
               ctx->event_log_buffer[idx].event.description,
               ctx->event_log_buffer[idx].event.confidence);
      lv_label_set_text(child, text);

      /* Set left border color to indicate alert level */

      lv_obj_set_style_border_side(child, LV_BORDER_SIDE_LEFT, 0);
      lv_obj_set_style_border_width(child, 3, 0);
      lv_obj_set_style_border_color(child, color, 0);
      lv_obj_set_style_pad_left(child, 5, 0);
    }

  /* If no events, show placeholder */

  if (ctx->event_log_count == 0)
    {
      child = lv_label_create(ctx->event_log);
      lv_obj_set_style_text_color(child, UI_COLOR_UNKNOWN, 0);
      lv_obj_set_style_text_font(child, &lv_font_montserrat_14, 0);
      lv_label_set_text(child, "No events detected");
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ui_init
 *
 * Description:
 *   Initialize the UI module.
 *
 * Output Parameters:
 *   handle - UI module handle
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int ui_init(ui_handle_t *handle)
{
  struct ui_ctx_s *ctx;
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result;

  DEBUGASSERT(handle != NULL);

  /* Allocate context */

  ctx = (struct ui_ctx_s *)malloc(sizeof(struct ui_ctx_s));
  if (ctx == NULL)
    {
      syslog(LOG_ERR, "UI: malloc failed\n");
      return -ENOMEM;
    }

  memset(ctx, 0, sizeof(struct ui_ctx_s));

  /* Initialize LVGL */

  lv_init();

  /* Initialize NuttX display driver */

  /* Wait for LCD and touchscreen */

  {
    int fd_lcd = -1;
    int fd_inp = -1;
    int retry;
    for (retry = 0; retry < 50; retry++)
      {
        if (fd_lcd < 0)
          fd_lcd = open("/dev/lcd0", O_RDONLY);
        if (fd_inp < 0)
          fd_inp = open("/dev/input0", O_RDONLY);
        if (fd_lcd >= 0 && fd_inp >= 0)
          {
            close(fd_lcd);
            close(fd_inp);
            break;
          }
        syslog(LOG_INFO, "UI: Waiting... (%d/50)\n", retry + 1);
        usleep(200000);
      }
  }

  lv_nuttx_dsc_init(&info);
  info.fb_path = "/dev/lcd0";
  lv_nuttx_init(&info, &result);

  if (result.disp == NULL)
    {
      syslog(LOG_ERR, "UI: Display init failed\n");
      goto err_display;
    }

  /* Get the active screen */

  ctx->scr = lv_scr_act();
  lv_obj_add_style(ctx->scr, &ctx->style_bg, 0);

  /* Create UI elements */

  ui_create_styles(ctx);
  ui_create_status_bar(ctx);
  ui_create_alert_panel(ctx);
  ui_create_event_log(ctx);
  ui_create_test_button(ctx);

  /* Create refresh timer */

  ctx->refresh_timer = lv_timer_create(ui_refresh_timer_cb,
                                        UI_REFRESH_RATE_MS, ctx);
  if (ctx->refresh_timer == NULL)
    {
      syslog(LOG_ERR, "UI: Timer create failed\n");
      goto err_timer;
    }

  *handle = (ui_handle_t)ctx;

  syslog(LOG_INFO, "UI: Initialized successfully\n");
  return 0;

err_timer:
  lv_nuttx_deinit(&result);
err_display:
  free(ctx);
  return -ENOMEM;
}

/****************************************************************************
 * Name: ui_deinit
 *
 * Description:
 *   Deinitialize the UI module.
 *
 * Input Parameters:
 *   handle - UI module handle
 *
 ****************************************************************************/

void ui_deinit(ui_handle_t handle)
{
  struct ui_ctx_s *ctx = (struct ui_ctx_s *)handle;

  if (ctx == NULL)
    {
      return;
    }

  /* Delete timer */

  if (ctx->refresh_timer != NULL)
    {
      lv_timer_del(ctx->refresh_timer);
    }

  /* Delete UI objects */

  if (ctx->event_log != NULL)
    {
      lv_obj_del(ctx->event_log);
    }

  if (ctx->alert_panel != NULL)
    {
      lv_obj_del(ctx->alert_panel);
    }

  if (ctx->status_bar != NULL)
    {
      lv_obj_del(ctx->status_bar);
    }

  /* Delete styles */

  lv_style_reset(&ctx->style_alert);
  lv_style_reset(&ctx->style_text);
  lv_style_reset(&ctx->style_bg);

  /* Deinitialize LVGL */

  lv_deinit();
  free(ctx);

  syslog(LOG_INFO, "UI: Deinitialized\n");
}

/****************************************************************************
 * Name: ui_show_alert
 *
 * Description:
 *   Show an alert for a sound event.
 *
 * Input Parameters:
 *   handle - UI module handle
 *   event  - Sound event to display
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int ui_show_alert(ui_handle_t handle, const sound_event_t *event)
{
  struct ui_ctx_s *ctx = (struct ui_ctx_s *)handle;
  lv_color_t color;
  char text[128];

  DEBUGASSERT(ctx != NULL);
  DEBUGASSERT(event != NULL);

  /* Get alert color */

  color = ui_get_alert_color(event->alert_level);

  /* Update alert panel */

  lv_obj_set_style_bg_color(ctx->alert_panel, color, 0);
  lv_obj_set_style_bg_opa(ctx->alert_panel, LV_OPA_80, 0);

  /* Update icon */

  lv_label_set_text(ctx->alert_icon, ui_get_sound_icon(event->type));

  /* Update text */

  snprintf(text, sizeof(text), "%s\nConfidence: %d%%",
           event->description, event->confidence);
  lv_label_set_text(ctx->alert_text, text);

  /* Update confidence bar */

  lv_bar_set_value(ctx->confidence_bar, event->confidence, LV_ANIM_ON);
  lv_obj_set_style_bg_color(ctx->confidence_bar, color, LV_PART_INDICATOR);

  /* Show the alert panel */

  lv_obj_clear_flag(ctx->alert_panel, LV_OBJ_FLAG_HIDDEN);
  ctx->alert_visible = true;
  ctx->alert_start_time = lv_tick_get();

  /* Add to event log (ring buffer with overwrite) */

  ctx->event_log_buffer[ctx->event_log_index].event = *event;
  ctx->event_log_buffer[ctx->event_log_index].display_time =
    lv_tick_get();
  ctx->event_log_index = (ctx->event_log_index + 1) % UI_EVENT_LOG_MAX;
  if (ctx->event_log_count < UI_EVENT_LOG_MAX)
    {
      ctx->event_log_count++;
    }

  /* Update event log display */

  ui_update_event_log_display(ctx);

  return 0;
}

/****************************************************************************
 * Name: ui_show_unknown
 *
 * Description:
 *   Show an unknown sound alert.
 *
 * Input Parameters:
 *   handle - UI module handle
 *   event  - Sound event to display
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int ui_show_unknown(ui_handle_t handle, const sound_event_t *event)
{
  struct ui_ctx_s *ctx = (struct ui_ctx_s *)handle;
  char text[128];

  DEBUGASSERT(ctx != NULL);
  DEBUGASSERT(event != NULL);

  /* Update status bar */

  snprintf(text, sizeof(text), "Unknown Sound (Confidence: %d%%)",
           event->confidence);
  lv_label_set_text(ctx->status_label, text);

  return 0;
}

/****************************************************************************
 * Name: ui_update_status
 *
 * Description:
 *   Update the status bar text.
 *
 * Input Parameters:
 *   handle - UI module handle
 *   status - Status text
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int ui_update_status(ui_handle_t handle, const char *status)
{
  struct ui_ctx_s *ctx = (struct ui_ctx_s *)handle;

  DEBUGASSERT(ctx != NULL);
  DEBUGASSERT(status != NULL);

  lv_label_set_text(ctx->status_label, status);

  return 0;
}

/****************************************************************************
 * Name: ui_load_history
 *
 * Description:
 *   Load historical events into the UI.
 *
 * Input Parameters:
 *   handle - UI module handle
 *   events - Array of events to load
 *   count  - Number of events
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int ui_load_history(ui_handle_t handle, const sound_event_t *events,
                    int count)
{
  struct ui_ctx_s *ctx = (struct ui_ctx_s *)handle;
  int i;
  int max_load;

  DEBUGASSERT(ctx != NULL);

  if (events == NULL || count <= 0)
    {
      return -EINVAL;
    }

  /* Limit to maximum log size */

  max_load = (count < UI_EVENT_LOG_MAX) ? count : UI_EVENT_LOG_MAX;

  /* Clear current log */

  ctx->event_log_count = 0;
  ctx->event_log_index = 0;

  /* Load events (only the most recent ones) */

  for (i = 0; i < max_load; i++)
    {
      int src_idx = count - max_load + i;

      ctx->event_log_buffer[i].event = events[src_idx];
      ctx->event_log_buffer[i].display_time = 0; /* Unknown time */
    }

  ctx->event_log_count = max_load;
  ctx->event_log_index = max_load % UI_EVENT_LOG_MAX;

  /* Update display */

  ui_update_event_log_display(ctx);

  syslog(LOG_INFO, "UI: Loaded %d historical events\n", max_load);
  return 0;
}
