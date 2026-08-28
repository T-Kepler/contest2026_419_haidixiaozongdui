/****************************************************************************
 * apps/soundwatch/components/vibration/vibration.c
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
#include <nuttx/timers/pwm.h>

#include "soundwatch.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define VIBRATION_PWM_CHANNEL    0
#define VIBRATION_PWM_FREQUENCY  200  /* Hz */
#define VIBRATION_PWM_DUTY       50   /* Percent */

/* Vibration patterns (milliseconds) */

#define PATTERN_FIRE_ALARM_ON    500
#define PATTERN_FIRE_ALARM_OFF   100
#define PATTERN_CAR_HORN_ON      300
#define PATTERN_CAR_HORN_OFF     200
#define PATTERN_DOORBELL_ON      150
#define PATTERN_DOORBELL_OFF     100
#define PATTERN_HUMAN_CALL_ON    200
#define PATTERN_HUMAN_CALL_OFF   150
#define PATTERN_UNKNOWN_ON       100
#define PATTERN_UNKNOWN_OFF      200

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Vibration pattern */

typedef struct vibration_pattern_s
{
  uint16_t on_time;    /* Vibration on time (ms) */
  uint16_t off_time;   /* Vibration off time (ms) */
  uint8_t  repeat;     /* Number of repetitions */
} vibration_pattern_t;

/* Vibration context */

struct vibration_ctx_s
{
  int      fd;         /* PWM device file descriptor */
  bool     active;     /* Vibration active flag */
  uint8_t  sound_type; /* Current sound type */
  struct vibration_pattern_s patterns[SOUND_TYPE_MAX];
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int vibration_set_pwm(struct vibration_ctx_s *ctx, bool enable);
static void vibration_task(struct vibration_ctx_s *ctx,
                           const vibration_pattern_t *pattern);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: vibration_set_pwm
 *
 * Description:
 *   Enable or disable PWM output.
 *
 * Input Parameters:
 *   ctx    - Vibration context
 *   enable - true to enable, false to disable
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

static int vibration_set_pwm(struct vibration_ctx_s *ctx, bool enable)
{
  struct pwm_info_s info;
  int ret;

  DEBUGASSERT(ctx != NULL);

  if (enable)
    {
      /* Configure PWM */

      info.frequency = VIBRATION_PWM_FREQUENCY;
      info.duty = VIBRATION_PWM_DUTY * 1000 / 100; /* Convert to 0-1000 */

      ret = ioctl(ctx->fd, PWMIOC_SETCHARACTERISTICS, (unsigned long)&info);
      if (ret < 0)
        {
          syslog(LOG_ERR, "Vibration: PWM set characteristics failed: %d\n",
                 errno);
          return -errno;
        }

      /* Start PWM */

      ret = ioctl(ctx->fd, PWMIOC_START, 0);
      if (ret < 0)
        {
          syslog(LOG_ERR, "Vibration: PWM start failed: %d\n", errno);
          return -errno;
        }
    }
  else
    {
      /* Stop PWM */

      ret = ioctl(ctx->fd, PWMIOC_STOP, 0);
      if (ret < 0)
        {
          syslog(LOG_ERR, "Vibration: PWM stop failed: %d\n", errno);
          return -errno;
        }
    }

  return 0;
}

/****************************************************************************
 * Name: vibration_task
 *
 * Description:
 *   Execute a vibration pattern.
 *
 * Input Parameters:
 *   ctx     - Vibration context
 *   pattern - Vibration pattern to execute
 *
 ****************************************************************************/

static void vibration_task(struct vibration_ctx_s *ctx,
                           const vibration_pattern_t *pattern)
{
  int i;

  DEBUGASSERT(ctx != NULL);
  DEBUGASSERT(pattern != NULL);

  ctx->active = true;

  for (i = 0; i < pattern->repeat && ctx->active; i++)
    {
      /* Vibrate on */

      vibration_set_pwm(ctx, true);
      usleep(pattern->on_time * 1000);

      /* Vibrate off */

      vibration_set_pwm(ctx, false);
      usleep(pattern->off_time * 1000);
    }

  ctx->active = false;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: vibration_init
 *
 * Description:
 *   Initialize the vibration module.
 *
 * Output Parameters:
 *   handle - Vibration module handle
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int vibration_init(vibration_handle_t *handle)
{
  struct vibration_ctx_s *ctx;
  int ret;

  DEBUGASSERT(handle != NULL);

  /* Allocate context */

  ctx = (struct vibration_ctx_s *)malloc(sizeof(struct vibration_ctx_s));
  if (ctx == NULL)
    {
      syslog(LOG_ERR, "Vibration: malloc failed\n");
      return -ENOMEM;
    }

  memset(ctx, 0, sizeof(struct vibration_ctx_s));

  /* Open PWM device */

  ctx->fd = open("/dev/pwm0", O_RDONLY);
  if (ctx->fd < 0)
    {
      syslog(LOG_ERR, "Vibration: Open /dev/pwm0 failed: %d\n", errno);
      ret = -errno;
      goto err_open;
    }

  ctx->active = false;
  ctx->sound_type = SOUND_TYPE_UNKNOWN;

  /* Initialize vibration patterns */

  /* Fire alarm: continuous strong vibration */

  ctx->patterns[SOUND_TYPE_FIRE_ALARM].on_time = PATTERN_FIRE_ALARM_ON;
  ctx->patterns[SOUND_TYPE_FIRE_ALARM].off_time = PATTERN_FIRE_ALARM_OFF;
  ctx->patterns[SOUND_TYPE_FIRE_ALARM].repeat = 5;

  /* Car horn: two long vibrations */

  ctx->patterns[SOUND_TYPE_CAR_HORN].on_time = PATTERN_CAR_HORN_ON;
  ctx->patterns[SOUND_TYPE_CAR_HORN].off_time = PATTERN_CAR_HORN_OFF;
  ctx->patterns[SOUND_TYPE_CAR_HORN].repeat = 2;

  /* Doorbell: two short vibrations */

  ctx->patterns[SOUND_TYPE_DOORBELL].on_time = PATTERN_DOORBELL_ON;
  ctx->patterns[SOUND_TYPE_DOORBELL].off_time = PATTERN_DOORBELL_OFF;
  ctx->patterns[SOUND_TYPE_DOORBELL].repeat = 2;

  /* Human call: three medium vibrations */

  ctx->patterns[SOUND_TYPE_HUMAN_CALL].on_time = PATTERN_HUMAN_CALL_ON;
  ctx->patterns[SOUND_TYPE_HUMAN_CALL].off_time = PATTERN_HUMAN_CALL_OFF;
  ctx->patterns[SOUND_TYPE_HUMAN_CALL].repeat = 3;

  /* Unknown: single short vibration */

  ctx->patterns[SOUND_TYPE_UNKNOWN].on_time = PATTERN_UNKNOWN_ON;
  ctx->patterns[SOUND_TYPE_UNKNOWN].off_time = PATTERN_UNKNOWN_OFF;
  ctx->patterns[SOUND_TYPE_UNKNOWN].repeat = 1;

  *handle = (vibration_handle_t)ctx;

  syslog(LOG_INFO, "Vibration: Initialized successfully\n");
  return 0;

err_open:
  free(ctx);
  return ret;
}

/****************************************************************************
 * Name: vibration_deinit
 *
 * Description:
 *   Deinitialize the vibration module.
 *
 * Input Parameters:
 *   handle - Vibration module handle
 *
 ****************************************************************************/

void vibration_deinit(vibration_handle_t handle)
{
  struct vibration_ctx_s *ctx = (struct vibration_ctx_s *)handle;

  if (ctx == NULL)
    {
      return;
    }

  /* Stop any active vibration */

  ctx->active = false;
  vibration_set_pwm(ctx, false);

  close(ctx->fd);
  free(ctx);

  syslog(LOG_INFO, "Vibration: Deinitialized\n");
}

/****************************************************************************
 * Name: vibration_alert
 *
 * Description:
 *   Trigger a vibration alert for a sound type.
 *
 * Input Parameters:
 *   handle    - Vibration module handle
 *   sound_type - Sound type to alert for
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int vibration_alert(vibration_handle_t handle, uint8_t sound_type)
{
  struct vibration_ctx_s *ctx = (struct vibration_ctx_s *)handle;

  DEBUGASSERT(ctx != NULL);

  if (sound_type >= SOUND_TYPE_MAX)
    {
      return -EINVAL;
    }

  /* Stop any current vibration */

  ctx->active = false;
  usleep(10000); /* 10ms delay */

  /* Execute the vibration pattern */

  vibration_task(ctx, &ctx->patterns[sound_type]);

  return 0;
}

/****************************************************************************
 * Name: vibration_stop
 *
 * Description:
 *   Stop any active vibration.
 *
 * Input Parameters:
 *   handle - Vibration module handle
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int vibration_stop(vibration_handle_t handle)
{
  struct vibration_ctx_s *ctx = (struct vibration_ctx_s *)handle;

  DEBUGASSERT(ctx != NULL);

  ctx->active = false;
  vibration_set_pwm(ctx, false);

  return 0;
}
