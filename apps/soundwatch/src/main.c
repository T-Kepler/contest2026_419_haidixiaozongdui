/****************************************************************************
 * apps/soundwatch/src/main.c
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
#include <unistd.h>
#include <fcntl.h>
#include <syslog.h>
#include <errno.h>
#include <time.h>
#include <lvgl/lvgl.h>

#include "soundwatch.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SOUNDWATCH_TASK_NAME     "soundwatch"

#ifdef CONFIG_APP_SOUNDWATCH_STACKSIZE
#  define SOUNDWATCH_STACK_SIZE    CONFIG_APP_SOUNDWATCH_STACKSIZE
#else
#  define SOUNDWATCH_STACK_SIZE    8192
#endif

#ifdef CONFIG_APP_SOUNDWATCH_PRIORITY
#  define SOUNDWATCH_PRIORITY      CONFIG_APP_SOUNDWATCH_PRIORITY
#else
#  define SOUNDWATCH_PRIORITY      100
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

enum soundwatch_state_e
{
  STATE_INIT = 0,
  STATE_IDLE,
  STATE_LISTENING,
  STATE_RECOGNIZING,
  STATE_ALERTING,
  STATE_ERROR
};

struct soundwatch_ctx_s
{
  enum soundwatch_state_e state;
  bool                  running;
  bool                  test_mode;
  uint8_t               test_index;
  audio_handle_t        audio;
  recognition_handle_t  recognition;
  vibration_handle_t    vibration;
  ui_handle_t           ui;
  storage_handle_t      storage;
  bluetooth_handle_t    bluetooth;
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static struct soundwatch_ctx_s *g_ctx_ptr = NULL;

void test_button_cb(lv_event_t *e)
{
  if (g_ctx_ptr == NULL)
    {
      return;
    }

  g_ctx_ptr->test_mode = !g_ctx_ptr->test_mode;
  if (g_ctx_ptr->test_mode)
    {
      syslog(LOG_INFO, "SoundWatch: Test mode ON");
      ui_update_status(g_ctx_ptr->ui, "TEST MODE - Tap to stop");
    }
  else
    {
      syslog(LOG_INFO, "SoundWatch: Test mode OFF");
      ui_update_status(g_ctx_ptr->ui, "SoundWatch - Listening...");
    }
}

static int soundwatch_init(struct soundwatch_ctx_s *ctx);
static void soundwatch_deinit(struct soundwatch_ctx_s *ctx);
static int soundwatch_process_event(struct soundwatch_ctx_s *ctx,
                                     const sound_event_t *event);
static int soundwatch_task(int argc, char *argv[]);

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct soundwatch_ctx_s g_soundwatch_ctx;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* Global test mode toggle (called from LVGL touch callback) */

static int soundwatch_init(struct soundwatch_ctx_s *ctx)
{
  int ret;

  DEBUGASSERT(ctx != NULL);

  syslog(LOG_INFO, "SoundWatch: Initializing...\n");

  ret = audio_init(&ctx->audio);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SoundWatch: Audio init failed: %d\n", ret);
      return ret;
    }

  ret = recognition_init(&ctx->recognition);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SoundWatch: Recognition init failed: %d\n", ret);
      goto err_recognition;
    }

  ret = vibration_init(&ctx->vibration);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SoundWatch: Vibration init failed: %d\n", ret);
      goto err_vibration;
    }

  ret = ui_init(&ctx->ui);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SoundWatch: UI init failed: %d\n", ret);
      goto err_ui;
    }

  ret = storage_init(&ctx->storage);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SoundWatch: Storage init failed: %d\n", ret);
      goto err_storage;
    }

  ret = bluetooth_init(&ctx->bluetooth);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SoundWatch: Bluetooth init failed: %d\n", ret);
      goto err_bluetooth;
    }

  ctx->state = STATE_IDLE;
  ctx->running = true;
  ctx->test_mode = true;
  ctx->test_index = 0;
  g_ctx_ptr = ctx;
  syslog(LOG_INFO, "SoundWatch: Test mode auto-started");

  syslog(LOG_INFO, "SoundWatch: Initialization complete\n");
  return 0;

err_bluetooth:
  storage_deinit(ctx->storage);
err_storage:
  ui_deinit(ctx->ui);
err_ui:
  vibration_deinit(ctx->vibration);
err_vibration:
  recognition_deinit(ctx->recognition);
err_recognition:
  audio_deinit(ctx->audio);
  return ret;
}

static void soundwatch_deinit(struct soundwatch_ctx_s *ctx)
{
  DEBUGASSERT(ctx != NULL);

  syslog(LOG_INFO, "SoundWatch: Deinitializing...\n");

  ctx->running = false;

  bluetooth_deinit(ctx->bluetooth);
  storage_deinit(ctx->storage);
  ui_deinit(ctx->ui);
  vibration_deinit(ctx->vibration);
  recognition_deinit(ctx->recognition);
  audio_deinit(ctx->audio);

  syslog(LOG_INFO, "SoundWatch: Deinitialization complete\n");
}

static int soundwatch_process_event(struct soundwatch_ctx_s *ctx,
                                     const sound_event_t *event)
{
  int ret;

  DEBUGASSERT(ctx != NULL);
  DEBUGASSERT(event != NULL);

  syslog(LOG_INFO, "SoundWatch: Processing event type=%d confidence=%d\n",
         event->type, event->confidence);

  ret = storage_add_event(ctx->storage, event);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SoundWatch: Storage add event failed: %d\n", ret);
    }

  if (event->confidence < CONFIG_SOUNDWATCH_CONFIDENCE_THRESHOLD)
    {
      syslog(LOG_INFO, "SoundWatch: Low confidence, showing unknown\n");
      ui_show_unknown(ctx->ui, event);
      return 0;
    }

  ret = vibration_alert(ctx->vibration, event->type);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SoundWatch: Vibration alert failed: %d\n", ret);
    }

  ret = ui_show_alert(ctx->ui, event);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SoundWatch: UI show alert failed: %d\n", ret);
    }

  bluetooth_notify_event(ctx->bluetooth, event);

  return 0;
}

static int soundwatch_task(int argc, char *argv[])
{
  struct soundwatch_ctx_s *ctx = &g_soundwatch_ctx;
  sound_event_t event;
  audio_frame_t frame;
  int ret;


  ret = soundwatch_init(ctx);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SoundWatch: Init failed, exiting\n");
      return 0;
    }

  while (ctx->running)
    {
      /* Process LVGL timers and flush display */

      lv_timer_handler();








      /* In test mode, generate fake events */

      if (ctx->test_mode && ctx->state == STATE_LISTENING)
        {
          sound_event_t test_event;
          memset(&test_event, 0, sizeof(test_event));

          test_event.type = (ctx->test_index % 4) + 1;
          test_event.confidence = 85 + (ctx->test_index % 15);
          test_event.timestamp = (uint32_t)time(NULL);

          switch (test_event.type)
            {
              case SOUND_TYPE_FIRE_ALARM:
                test_event.alert_level = ALERT_LEVEL_CRITICAL;
                strncpy(test_event.description, "Fire Alarm",
                        sizeof(test_event.description));
                break;
              case SOUND_TYPE_CAR_HORN:
                test_event.alert_level = ALERT_LEVEL_HIGH;
                strncpy(test_event.description, "Car Horn",
                        sizeof(test_event.description));
                break;
              case SOUND_TYPE_DOORBELL:
                test_event.alert_level = ALERT_LEVEL_MEDIUM;
                strncpy(test_event.description, "Doorbell",
                        sizeof(test_event.description));
                break;
              case SOUND_TYPE_HUMAN_CALL:
                test_event.alert_level = ALERT_LEVEL_MEDIUM;
                strncpy(test_event.description, "Human Call",
                        sizeof(test_event.description));
                break;
            }

          syslog(LOG_INFO, "SoundWatch: Test event type=%d\n",
                 test_event.type);
          soundwatch_process_event(ctx, &test_event);
          ctx->test_index++;

          usleep(2000000);
          continue;
        }

      switch (ctx->state)
        {
          case STATE_IDLE:
            ret = audio_start(ctx->audio);
            if (ret < 0)
              {
                syslog(LOG_ERR, "SoundWatch: Audio start failed: %d\n", ret);
                ctx->state = STATE_ERROR;
                break;
              }
            ctx->state = STATE_LISTENING;
            break;

          case STATE_LISTENING:
            ret = audio_read(ctx->audio, &frame, sizeof(frame));
            if (ret < 0)
              {
                if (ret == -EAGAIN)
                  {
                    usleep(10000);
                    break;
                  }
                syslog(LOG_ERR, "SoundWatch: Audio read failed: %d\n", ret);
                ctx->state = STATE_ERROR;
                break;
              }

            ret = recognition_process(ctx->recognition, &frame, &event);
            if (ret < 0)
              {
                break;
              }

            ctx->state = STATE_ALERTING;
            break;

          case STATE_ALERTING:
            ret = soundwatch_process_event(ctx, &event);
            if (ret < 0)
              {
                syslog(LOG_ERR, "SoundWatch: Process event failed: %d\n",
                       ret);
              }
            ctx->state = STATE_LISTENING;
            break;

          case STATE_ERROR:
            syslog(LOG_ERR, "SoundWatch: In error state, attempting recovery\n");
            audio_stop(ctx->audio);
            usleep(1000000);
            ctx->state = STATE_IDLE;
            break;

          default:
            syslog(LOG_ERR, "SoundWatch: Unknown state: %d\n", ctx->state);
            ctx->state = STATE_IDLE;
            break;
        }

      usleep(10000);
    }

  soundwatch_deinit(ctx);
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, char *argv[])
{
  syslog(LOG_INFO, "SoundWatch: Starting...\n");

  int ret = soundwatch_task(argc, argv);
  if (ret < 0)
    {
      return EXIT_FAILURE;
    }

  return EXIT_SUCCESS;
}
