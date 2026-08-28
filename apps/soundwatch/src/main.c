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
#include <syslog.h>
#include <errno.h>

#include "soundwatch.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SOUNDWATCH_TASK_NAME     "soundwatch"

#ifdef CONFIG_APP_SOUNDWATCH_STACKSIZE
#  define SOUNDWATCH_STACK_SIZE    CONFIG_APP_SOUNDWATCH_STACKSIZE
#else
#  define SOUNDWATCH_STACK_SIZE    4096
#endif

#ifdef CONFIG_APP_SOUNDWATCH_PRIORITY
#  define SOUNDWATCH_PRIORITY      CONFIG_APP_SOUNDWATCH_PRIORITY
#else
#  define SOUNDWATCH_PRIORITY      100
#endif

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Application state machine states */

enum soundwatch_state_e
{
  STATE_INIT = 0,          /* Initialization state */
  STATE_IDLE,              /* Idle state, waiting for events */
  STATE_LISTENING,         /* Audio listening state */
  STATE_RECOGNIZING,       /* Sound recognition state */
  STATE_ALERTING,          /* Alert state */
  STATE_ERROR              /* Error state */
};

/* Application context */

struct soundwatch_ctx_s
{
  enum soundwatch_state_e state;     /* Current state */
  bool                  running;     /* Running flag */
  audio_handle_t        audio;       /* Audio module handle */
  recognition_handle_t  recognition; /* Recognition module handle */
  vibration_handle_t    vibration;   /* Vibration module handle */
  ui_handle_t           ui;          /* UI module handle */
  storage_handle_t      storage;     /* Storage module handle */
  bluetooth_handle_t    bluetooth;   /* Bluetooth module handle */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

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

/****************************************************************************
 * Name: soundwatch_init
 *
 * Description:
 *   Initialize all soundwatch modules.
 *
 * Input Parameters:
 *   ctx - Application context
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

static int soundwatch_init(struct soundwatch_ctx_s *ctx)
{
  int ret;

  DEBUGASSERT(ctx != NULL);

  syslog(LOG_INFO, "SoundWatch: Initializing...\n");

  /* Initialize audio module */

  ret = audio_init(&ctx->audio);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SoundWatch: Audio init failed: %d\n", ret);
      return ret;
    }

  /* Initialize recognition module */

  ret = recognition_init(&ctx->recognition);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SoundWatch: Recognition init failed: %d\n", ret);
      goto err_recognition;
    }

  /* Initialize vibration module */

  ret = vibration_init(&ctx->vibration);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SoundWatch: Vibration init failed: %d\n", ret);
      goto err_vibration;
    }

  /* Initialize UI module */

  ret = ui_init(&ctx->ui);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SoundWatch: UI init failed: %d\n", ret);
      goto err_ui;
    }

  /* Initialize storage module */

  ret = storage_init(&ctx->storage);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SoundWatch: Storage init failed: %d\n", ret);
      goto err_storage;
    }

  /* Initialize bluetooth module */

  ret = bluetooth_init(&ctx->bluetooth);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SoundWatch: Bluetooth init failed: %d\n", ret);
      goto err_bluetooth;
    }

  ctx->state = STATE_IDLE;
  ctx->running = true;

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

/****************************************************************************
 * Name: soundwatch_deinit
 *
 * Description:
 *   Deinitialize all soundwatch modules.
 *
 * Input Parameters:
 *   ctx - Application context
 *
 ****************************************************************************/

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

/****************************************************************************
 * Name: soundwatch_process_event
 *
 * Description:
 *   Process a sound event.
 *
 * Input Parameters:
 *   ctx   - Application context
 *   event - Sound event to process
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

static int soundwatch_process_event(struct soundwatch_ctx_s *ctx,
                                     const sound_event_t *event)
{
  int ret;

  DEBUGASSERT(ctx != NULL);
  DEBUGASSERT(event != NULL);

  syslog(LOG_INFO, "SoundWatch: Processing event type=%d confidence=%d\n",
         event->type, event->confidence);

  /* Store the event */

  ret = storage_add_event(ctx->storage, event);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SoundWatch: Storage add event failed: %d\n", ret);
    }

  /* Check confidence threshold */

  if (event->confidence < SOUNDWATCH_CONFIDENCE_THRESHOLD)
    {
      syslog(LOG_INFO, "SoundWatch: Low confidence, showing unknown\n");
      ui_show_unknown(ctx->ui, event);
      return 0;
    }

  /* Trigger vibration alert */

  ret = vibration_alert(ctx->vibration, event->type);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SoundWatch: Vibration alert failed: %d\n", ret);
    }

  /* Update UI */

  ret = ui_show_alert(ctx->ui, event);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SoundWatch: UI show alert failed: %d\n", ret);
    }

  /* Notify bluetooth if connected */

  bluetooth_notify_event(ctx->bluetooth, event);

  return 0;
}

/****************************************************************************
 * Name: soundwatch_task
 *
 * Description:
 *   Main soundwatch task.
 *
 * Input Parameters:
 *   argc - Argument count
 *   argv - Argument vector
 *
 ****************************************************************************/

static int soundwatch_task(int argc, char *argv[])
{
  struct soundwatch_ctx_s *ctx = &g_soundwatch_ctx;
  sound_event_t event;
  audio_frame_t frame;
  int ret;

  /* Initialize the application */

  ret = soundwatch_init(ctx);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SoundWatch: Init failed, exiting\n");
      return 0;
    }

  /* Main loop */

  while (ctx->running)
    {
      switch (ctx->state)
        {
          case STATE_IDLE:
            /* Start listening for audio */

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
            /* Read audio frame */

            ret = audio_read(ctx->audio, &frame, sizeof(frame));
            if (ret < 0)
              {
                if (ret == -EAGAIN)
                  {
                    /* No data available, continue */

                    usleep(10000); /* 10ms */
                    break;
                  }

                syslog(LOG_ERR, "SoundWatch: Audio read failed: %d\n", ret);
                ctx->state = STATE_ERROR;
                break;
              }

            /* Process the audio frame */

            ret = recognition_process(ctx->recognition, &frame, &event);
            if (ret < 0)
              {
                /* No event detected, continue listening */

                break;
              }

            /* Event detected, transition to alerting state */

            ctx->state = STATE_ALERTING;
            break;

          case STATE_ALERTING:
            /* Process the detected event */

            ret = soundwatch_process_event(ctx, &event);
            if (ret < 0)
              {
                syslog(LOG_ERR, "SoundWatch: Process event failed: %d\n",
                       ret);
              }

            /* Return to listening state */

            ctx->state = STATE_LISTENING;
            break;

          case STATE_ERROR:
            /* Error state, try to recover */

            syslog(LOG_ERR, "SoundWatch: In error state, attempting recovery\n");

            audio_stop(ctx->audio);
            usleep(1000000); /* 1 second */

            ctx->state = STATE_IDLE;
            break;

          default:
            syslog(LOG_ERR, "SoundWatch: Unknown state: %d\n", ctx->state);
            ctx->state = STATE_IDLE;
            break;
        }
    }

  /* Deinitialize the application */

  soundwatch_deinit(ctx);

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: main
 *
 * Description:
 *   SoundWatch application entry point.
 *
 ****************************************************************************/

int main(int argc, char *argv[])
{
  int ret;
  int status;

  syslog(LOG_INFO, "SoundWatch: Starting...\n");

  /* Start the soundwatch task */

  ret = task_create(SOUNDWATCH_TASK_NAME,
                    SOUNDWATCH_PRIORITY,
                    SOUNDWATCH_STACK_SIZE,
                    soundwatch_task,
                    NULL);
  if (ret < 0)
    {
      syslog(LOG_ERR, "SoundWatch: Task create failed: %d\n", errno);
      return EXIT_FAILURE;
    }

  /* Wait for the task to complete */

  waitpid(ret, &status, 0);

  syslog(LOG_INFO, "SoundWatch: Task exited with status %d\n", status);

  return EXIT_SUCCESS;
}
