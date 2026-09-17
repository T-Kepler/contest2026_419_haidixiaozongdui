/****************************************************************************
 * apps/soundwatch/include/soundwatch.h
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

#ifndef __APPS_SOUNDWATCH_INCLUDE_SOUNDWATCH_H
#define __APPS_SOUNDWATCH_INCLUDE_SOUNDWATCH_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Sound types */

#define SOUND_TYPE_UNKNOWN        0
#define SOUND_TYPE_FIRE_ALARM     1
#define SOUND_TYPE_CAR_HORN       2
#define SOUND_TYPE_DOORBELL       3
#define SOUND_TYPE_HUMAN_CALL     4
#define SOUND_TYPE_MAX            5

/* Alert levels */

#define ALERT_LEVEL_LOW           0
#define ALERT_LEVEL_MEDIUM        1
#define ALERT_LEVEL_HIGH          2
#define ALERT_LEVEL_CRITICAL      3

/* Configuration defaults - use Kconfig values if available */

#ifdef CONFIG_SOUNDWATCH_CONFIDENCE_THRESHOLD
#  define SOUNDWATCH_CONFIDENCE_THRESHOLD   CONFIG_SOUNDWATCH_CONFIDENCE_THRESHOLD
#else
#  define SOUNDWATCH_CONFIDENCE_THRESHOLD   70
#endif

#ifdef CONFIG_SOUNDWATCH_COOLDOWN_MS
#  define SOUNDWATCH_COOLDOWN_MS            CONFIG_SOUNDWATCH_COOLDOWN_MS
#else
#  define SOUNDWATCH_COOLDOWN_MS            5000
#endif

#ifdef CONFIG_SOUNDWATCH_CONFIRM_FRAMES
#  define SOUNDWATCH_CONFIRM_FRAMES         CONFIG_SOUNDWATCH_CONFIRM_FRAMES
#else
#  define SOUNDWATCH_CONFIRM_FRAMES         3
#endif

#ifdef CONFIG_SOUNDWATCH_MAX_EVENTS
#  define SOUNDWATCH_MAX_EVENTS             CONFIG_SOUNDWATCH_MAX_EVENTS
#else
#  define SOUNDWATCH_MAX_EVENTS             100
#endif

/* Audio configuration */

#define AUDIO_SAMPLE_RATE         16000
#define AUDIO_CHANNELS            1
#define AUDIO_BITS_PER_SAMPLE     16
#define AUDIO_FRAME_SIZE          512

/* Feature extraction configuration */

#define FEATURE_MFCC_COEFFS       13
#define FEATURE_MEL_BANDS         40
#define FEATURE_FFT_SIZE          512

/****************************************************************************
 * Public Types
 ****************************************************************************/

/* Sound event structure */

typedef struct sound_event_s
{
  uint8_t  type;           /* Sound type */
  uint8_t  confidence;     /* Confidence level (0-100) */
  uint8_t  alert_level;    /* Alert level */
  uint32_t timestamp;      /* Event timestamp */
  char     description[64]; /* Event description */
} sound_event_t;

/* Audio frame structure */

typedef struct audio_frame_s
{
  int16_t  data[AUDIO_FRAME_SIZE]; /* Audio samples */
  uint32_t size;                    /* Frame size in bytes */
  uint32_t timestamp;               /* Frame timestamp */
} audio_frame_t;

/* Feature vector structure */

typedef struct feature_vector_s
{
  float mfcc[FEATURE_MFCC_COEFFS]; /* MFCC coefficients */
  float energy;                     /* Frame energy */
  uint32_t timestamp;               /* Feature timestamp */
} feature_vector_t;

/* Module handle types */

typedef void *audio_handle_t;
typedef void *recognition_handle_t;
typedef void *vibration_handle_t;
typedef void *ui_handle_t;
typedef void *storage_handle_t;
typedef void *bluetooth_handle_t;

/* Event callback type */

typedef void (*event_callback_t)(const sound_event_t *event, void *userdata);

/****************************************************************************
 * Public Function Prototypes - Audio Module
 ****************************************************************************/

int audio_init(audio_handle_t *handle);
void audio_deinit(audio_handle_t handle);
int audio_start(audio_handle_t handle);
int audio_stop(audio_handle_t handle);
int audio_read(audio_handle_t handle, audio_frame_t *frame, size_t size);

/****************************************************************************
 * Public Function Prototypes - Recognition Module
 ****************************************************************************/

int recognition_init(recognition_handle_t *handle);
void recognition_deinit(recognition_handle_t handle);
int recognition_process(recognition_handle_t handle,
                        const audio_frame_t *frame,
                        sound_event_t *event);
int recognition_set_threshold(recognition_handle_t handle, uint8_t threshold);

/****************************************************************************
 * Public Function Prototypes - Vibration Module
 ****************************************************************************/

int vibration_init(vibration_handle_t *handle);
void vibration_deinit(vibration_handle_t handle);
int vibration_alert(vibration_handle_t handle, uint8_t sound_type);
int vibration_stop(vibration_handle_t handle);

/****************************************************************************
 * Public Function Prototypes - UI Module
 ****************************************************************************/

int ui_init(ui_handle_t *handle);
void ui_deinit(ui_handle_t handle);
int ui_show_alert(ui_handle_t handle, const sound_event_t *event);
int ui_show_unknown(ui_handle_t handle, const sound_event_t *event);
int ui_update_status(ui_handle_t handle, const char *status);
int ui_load_history(ui_handle_t handle, const sound_event_t *events,
                    int count);

/****************************************************************************
 * Public Function Prototypes - Storage Module
 ****************************************************************************/

int storage_init(storage_handle_t *handle);
void storage_deinit(storage_handle_t handle);
int storage_add_event(storage_handle_t handle, const sound_event_t *event);
int storage_get_events(storage_handle_t handle, sound_event_t *events,
                       int max_events, int *count);
int storage_clear_events(storage_handle_t handle);

/****************************************************************************
 * Public Function Prototypes - Bluetooth Module
 ****************************************************************************/

int bluetooth_init(bluetooth_handle_t *handle);
void bluetooth_deinit(bluetooth_handle_t handle);
int bluetooth_notify_event(bluetooth_handle_t handle,
                           const sound_event_t *event);
int bluetooth_is_connected(bluetooth_handle_t handle);

#endif /* __APPS_SOUNDWATCH_INCLUDE_SOUNDWATCH_H */
