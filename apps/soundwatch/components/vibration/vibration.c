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
#include <pthread.h>
#include <nuttx/timers/pwm.h>

#include "soundwatch.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define VIBRATION_PWM_CHANNEL    0
#define VIBRATION_PWM_FREQUENCY  200   /* Hz */

/* NuttX ub16_t: 0-65535 represents 0%-100% duty cycle */

#define VIBRATION_PWM_DUTY_50    32768 /* 50% = 32768/65536 */

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
  int      fd;              /* PWM device file descriptor */
  bool     active;          /* Vibration active flag */
  bool     thread_running;  /* Thread running flag */
  bool     thread_created;  /* Thread created flag */
  uint8_t  sound_type;      /* Current sound type */
  pthread_t thread;         /* Vibration thread */
  pthread_mutex_t mutex;    /* Mutex for thread safety */
  pthread_cond_t cond;      /* Condition variable for signaling */
  struct vibration_pattern_s current_pattern; /* Current pattern to execute */
  struct vibration_pattern_s patterns[SOUND_TYPE_MAX];
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int vibration_set_pwm(struct vibration_ctx_s *ctx, bool enable);

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

  /* Clear the structure before use */

  memset(&info, 0, sizeof(info));

  if (enable)
    {
      /* Configure PWM - duty is ub16_t format (0-65535 = 0%-100%) */

      info.frequency = VIBRATION_PWM_FREQUENCY;
      info.duty = VIBRATION_PWM_DUTY_50; /* 50% duty cycle */

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
 * Name: vibration_thread
 *
 * Description:
 *   Vibration thread function. Executes vibration patterns in a separate
 *   thread to avoid blocking the main thread.
 *
 * Input Parameters:
 *   arg - Vibration context pointer
 *
 * Returned Value:
 *   NULL always
 *
 ****************************************************************************/

static void *vibration_thread(void *arg)
{
  struct vibration_ctx_s *ctx = (struct vibration_ctx_s *)arg;
  vibration_pattern_t pattern;
  int i;

  DEBUGASSERT(ctx != NULL);

  /* Copy pattern under mutex */

  pthread_mutex_lock(&ctx->mutex);
  pattern = ctx->current_pattern;
  ctx->active = true;
  ctx->thread_running = true;
  pthread_cond_signal(&ctx->cond); /* Signal that thread has started */
  pthread_mutex_unlock(&ctx->mutex);

  for (i = 0; i < pattern.repeat; i++)
    {
      /* Check if still active under mutex */

      pthread_mutex_lock(&ctx->mutex);
      if (!ctx->active)
        {
          pthread_mutex_unlock(&ctx->mutex);
          break;
        }

      /* Keep PWM on while we hold the lock to prevent stop from interfering */

      vibration_set_pwm(ctx, true);
      pthread_mutex_unlock(&ctx->mutex);

      usleep(pattern.on_time * 1000);

      /* Check again before turning off */

      pthread_mutex_lock(&ctx->mutex);
      if (!ctx->active)
        {
          vibration_set_pwm(ctx, false);
          pthread_mutex_unlock(&ctx->mutex);
          break;
        }

      vibration_set_pwm(ctx, false);
      pthread_mutex_unlock(&ctx->mutex);

      usleep(pattern.off_time * 1000);
    }

  /* Ensure PWM is stopped */

  pthread_mutex_lock(&ctx->mutex);
  vibration_set_pwm(ctx, false);
  ctx->active = false;
  ctx->thread_running = false;
  pthread_cond_signal(&ctx->cond); /* Signal that thread has finished */
  pthread_mutex_unlock(&ctx->mutex);

  return NULL;
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
  ctx->thread_running = false;
  ctx->thread_created = false;
  ctx->sound_type = SOUND_TYPE_UNKNOWN;

  /* Initialize mutex and condition variable */

  pthread_mutex_init(&ctx->mutex, NULL);
  pthread_cond_init(&ctx->cond, NULL);

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

  /* Stop any active vibration and wait for thread to finish */

  pthread_mutex_lock(&ctx->mutex);

  /* Signal the thread to stop */

  ctx->active = false;

  /* Wait for thread to finish if it's running */

  while (ctx->thread_running)
    {
      pthread_cond_wait(&ctx->cond, &ctx->mutex);
    }

  /* Wait for thread to finish while still holding the lock */

  if (ctx->thread_created)
    {
      pthread_t thread = ctx->thread;
      ctx->thread_created = false;
      pthread_mutex_unlock(&ctx->mutex);
      pthread_join(thread, NULL);
    }
  else
    {
      pthread_mutex_unlock(&ctx->mutex);
    }

  /* Ensure PWM is stopped */

  vibration_set_pwm(ctx, false);

  /* Destroy synchronization primitives */

  pthread_cond_destroy(&ctx->cond);
  pthread_mutex_destroy(&ctx->mutex);

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
  int ret;

  DEBUGASSERT(ctx != NULL);

  if (sound_type >= SOUND_TYPE_MAX)
    {
      return -EINVAL;
    }

  /* Stop any current vibration and wait for thread to finish */

  pthread_mutex_lock(&ctx->mutex);

  /* Signal the thread to stop */

  ctx->active = false;

  /* Wait for thread to finish if it's running */

  while (ctx->thread_running)
    {
      pthread_cond_wait(&ctx->cond, &ctx->mutex);
    }

  /* Join the finished thread to release resources */

  if (ctx->thread_created)
    {
      pthread_t old_thread = ctx->thread;
      ctx->thread_created = false;
      pthread_mutex_unlock(&ctx->mutex);
      pthread_join(old_thread, NULL);
      pthread_mutex_lock(&ctx->mutex);
    }

  /* Copy pattern before starting thread */

  ctx->current_pattern = ctx->patterns[sound_type];

  pthread_mutex_unlock(&ctx->mutex);

  /* Start new thread */

  ret = pthread_create(&ctx->thread, NULL, vibration_thread, ctx);
  if (ret != 0)
    {
      syslog(LOG_ERR, "Vibration: Thread creation failed: %d\n", ret);
      return -ret;
    }

  /* Mark thread as created under mutex protection */

  pthread_mutex_lock(&ctx->mutex);
  ctx->thread_created = true;
  pthread_mutex_unlock(&ctx->mutex);

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

  pthread_mutex_lock(&ctx->mutex);

  /* Signal the thread to stop */

  ctx->active = false;

  /* Wait for thread to finish if it's running */

  while (ctx->thread_running)
    {
      pthread_cond_wait(&ctx->cond, &ctx->mutex);
    }

  /* Join the finished thread to release resources */

  if (ctx->thread_created)
    {
      pthread_t old_thread = ctx->thread;
      ctx->thread_created = false;
      pthread_mutex_unlock(&ctx->mutex);
      pthread_join(old_thread, NULL);
    }
  else
    {
      pthread_mutex_unlock(&ctx->mutex);
    }

  /* Ensure PWM is stopped */

  vibration_set_pwm(ctx, false);

  return 0;
}
