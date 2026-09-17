/****************************************************************************
 * apps/soundwatch/components/audio/audio.c
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
#include <errno.h>
#include <syslog.h>
#include <sys/ioctl.h>
#include <math.h>

#include "soundwatch.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Audio device context */

struct audio_ctx_s
{
  int      fd;          /* Audio device file descriptor (-1 = simulation) */
  bool     simulation;  /* Simulation mode flag */
  bool     running;     /* Recording flag */
  uint32_t sample_rate; /* Sample rate */
  uint8_t  channels;    /* Number of channels */
  uint8_t  bits;        /* Bits per sample */
  uint32_t frame_count; /* Frame counter for simulation */
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: audio_init
 *
 * Description:
 *   Initialize the audio module.  Tries real hardware first, falls back
 *   to simulation mode if no audio device is available.
 *
 ****************************************************************************/

int audio_init(audio_handle_t *handle)
{
  struct audio_ctx_s *ctx;
  int i;

  static const char *audio_devices[] =
  {
    "/dev/audio/pcmC0D0c",
    "/dev/audio0",
    "/dev/pcm0",
    NULL
  };

  DEBUGASSERT(handle != NULL);

  ctx = (struct audio_ctx_s *)malloc(sizeof(struct audio_ctx_s));
  if (ctx == NULL)
    {
      syslog(LOG_ERR, "Audio: malloc failed\n");
      return -ENOMEM;
    }

  memset(ctx, 0, sizeof(struct audio_ctx_s));
  ctx->fd = -1;
  ctx->simulation = true;
  ctx->sample_rate = AUDIO_SAMPLE_RATE;
  ctx->channels = AUDIO_CHANNELS;
  ctx->bits = AUDIO_BITS_PER_SAMPLE;
  ctx->running = false;
  ctx->frame_count = 0;

  /* Try to open a real audio capture device */

  for (i = 0; audio_devices[i] != NULL; i++)
    {
      ctx->fd = open(audio_devices[i], O_RDONLY);
      if (ctx->fd >= 0)
        {
          ctx->simulation = false;
          syslog(LOG_INFO, "Audio: Opened %s (real hardware)\n",
                 audio_devices[i]);
          break;
        }
    }

  *handle = (audio_handle_t)ctx;

  if (ctx->simulation)
    {
      syslog(LOG_INFO, "Audio: Initialized (simulation mode)\n");
    }

  return 0;
}

/****************************************************************************
 * Name: audio_deinit
 ****************************************************************************/

void audio_deinit(audio_handle_t handle)
{
  struct audio_ctx_s *ctx = (struct audio_ctx_s *)handle;

  if (ctx == NULL)
    {
      return;
    }

  if (ctx->running)
    {
      audio_stop(handle);
    }

  if (ctx->fd >= 0)
    {
      close(ctx->fd);
    }

  free(ctx);

  syslog(LOG_INFO, "Audio: Deinitialized\n");
}

/****************************************************************************
 * Name: audio_start
 ****************************************************************************/

int audio_start(audio_handle_t handle)
{
  struct audio_ctx_s *ctx = (struct audio_ctx_s *)handle;

  DEBUGASSERT(ctx != NULL);

  if (ctx->running)
    {
      return 0;
    }

  ctx->running = true;
  ctx->frame_count = 0;

  syslog(LOG_INFO, "Audio: Started recording%s\n",
         ctx->simulation ? " (simulation mode)" : "");
  return 0;
}

/****************************************************************************
 * Name: audio_stop
 ****************************************************************************/

int audio_stop(audio_handle_t handle)
{
  struct audio_ctx_s *ctx = (struct audio_ctx_s *)handle;

  DEBUGASSERT(ctx != NULL);

  if (!ctx->running)
    {
      return 0;
    }

  ctx->running = false;

  syslog(LOG_INFO, "Audio: Stopped recording\n");
  return 0;
}

/****************************************************************************
 * Name: audio_read
 *
 * Description:
 *   Read an audio frame.  If real hardware is available, reads from the
 *   device; otherwise generates a simulation sine wave.
 *
 ****************************************************************************/

int audio_read(audio_handle_t handle, audio_frame_t *frame, size_t size)
{
  struct audio_ctx_s *ctx = (struct audio_ctx_s *)handle;
  ssize_t bytes_read;
  int i;

  DEBUGASSERT(ctx != NULL);
  DEBUGASSERT(frame != NULL);

  if (!ctx->running)
    {
      return -EPERM;
    }

  /* Read from real audio device if available */

  if (!ctx->simulation && ctx->fd >= 0)
    {
      bytes_read = read(ctx->fd, frame->data,
                        AUDIO_FRAME_SIZE * sizeof(int16_t));
      if (bytes_read > 0)
        {
          frame->size = (uint32_t)bytes_read;
          frame->timestamp = (uint32_t)time(NULL);
          return (int)frame->size;
        }

      /* Read failed, fall back to simulation */

      syslog(LOG_WARNING, "Audio: read failed (%d), "
             "falling back to simulation\n", errno);
      ctx->simulation = true;
    }

  /* Simulate audio data (sine wave for testing) */

  ctx->frame_count++;

  for (i = 0; i < AUDIO_FRAME_SIZE; i++)
    {
      float t = (float)(ctx->frame_count * AUDIO_FRAME_SIZE + i) /
                (float)AUDIO_SAMPLE_RATE;
      frame->data[i] = (int16_t)(16000.0f *
                        sinf(2.0f * 3.14159f * 440.0f * t));
    }

  frame->size = AUDIO_FRAME_SIZE * sizeof(int16_t);
  frame->timestamp = (uint32_t)time(NULL);

  usleep(32000);

  return frame->size;
}
