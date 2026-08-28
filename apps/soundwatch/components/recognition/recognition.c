/****************************************************************************
 * apps/soundwatch/components/recognition/recognition.c
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
#include <math.h>
#include <errno.h>
#include <syslog.h>

#include "soundwatch.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MEL_LOW_FREQ       0
#define MEL_HIGH_FREQ      (AUDIO_SAMPLE_RATE / 2)
#define PRE_EMPHASIS_COEFF 0.97f
#define FRAME_ENERGY_THRESHOLD 100.0f

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Recognition context */

struct recognition_ctx_s
{
  uint8_t  threshold;           /* Confidence threshold */
  uint8_t  confirm_frames;      /* Number of frames to confirm */
  uint8_t  consecutive_count;   /* Consecutive detection count */
  uint8_t  last_type;           /* Last detected type */
  uint32_t last_timestamp;      /* Last detection timestamp */
  uint32_t cooldown_ms;         /* Cooldown time in milliseconds */

  /* Feature extraction buffers */

  float window[AUDIO_FRAME_SIZE];     /* Windowed signal */
  float fft_real[FEATURE_FFT_SIZE];   /* FFT real part */
  float fft_imag[FEATURE_FFT_SIZE];   /* FFT imaginary part */
  float mel_energies[FEATURE_MEL_BANDS]; /* Mel band energies */
  float mfcc[FEATURE_MFCC_COEFFS];    /* MFCC coefficients */

  /* Simple model weights (placeholder for real model) */

  float model_weights[SOUND_TYPE_MAX][FEATURE_MFCC_COEFFS];
  float model_bias[SOUND_TYPE_MAX];
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static float mel_to_hz(float mel);
static float hz_to_mel(float hz);
static void compute_mel_filterbank(struct recognition_ctx_s *ctx,
                                   const float *power_spectrum,
                                   float *mel_energies);
static void compute_mfcc(struct recognition_ctx_s *ctx,
                         const audio_frame_t *frame,
                         feature_vector_t *features);
static int classify_sound(struct recognition_ctx_s *ctx,
                          const feature_vector_t *features,
                          sound_event_t *event);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: mel_to_hz
 *
 * Description:
 *   Convert Mel frequency to Hz.
 *
 ****************************************************************************/

static float mel_to_hz(float mel)
{
  return 700.0f * (powf(10.0f, mel / 2595.0f) - 1.0f);
}

/****************************************************************************
 * Name: hz_to_mel
 *
 * Description:
 *   Convert Hz to Mel frequency.
 *
 ****************************************************************************/

static float hz_to_mel(float hz)
{
  return 2595.0f * log10f(1.0f + hz / 700.0f);
}

/****************************************************************************
 * Name: compute_mel_filterbank
 *
 * Description:
 *   Compute Mel filterbank energies.
 *
 ****************************************************************************/

static void compute_mel_filterbank(struct recognition_ctx_s *ctx,
                                   const float *power_spectrum,
                                   float *mel_energies)
{
  float mel_low = hz_to_mel(MEL_LOW_FREQ);
  float mel_high = hz_to_mel(MEL_HIGH_FREQ);
  float mel_step = (mel_high - mel_low) / (FEATURE_MEL_BANDS + 1);
  float fft_bin_freq = (float)AUDIO_SAMPLE_RATE / FEATURE_FFT_SIZE;
  int i, j;

  /* Initialize mel energies */

  for (i = 0; i < FEATURE_MEL_BANDS; i++)
    {
      mel_energies[i] = 0.0f;
    }

  /* Compute mel filterbank */

  for (i = 0; i < FEATURE_MEL_BANDS; i++)
    {
      float center_mel = mel_low + (i + 1) * mel_step;
      float center_hz = mel_to_hz(center_mel);
      float center_bin = center_hz / fft_bin_freq;
      float left_bin = mel_to_hz(center_mel - mel_step) / fft_bin_freq;
      float right_bin = mel_to_hz(center_mel + mel_step) / fft_bin_freq;

      for (j = 0; j < FEATURE_FFT_SIZE / 2; j++)
        {
          float weight = 0.0f;

          if (j >= left_bin && j <= center_bin)
            {
              weight = (j - left_bin) / (center_bin - left_bin);
            }
          else if (j > center_bin && j <= right_bin)
            {
              weight = (right_bin - j) / (right_bin - center_bin);
            }

          mel_energies[i] += power_spectrum[j] * weight;
        }

      /* Convert to log scale */

      if (mel_energies[i] > 0.0f)
        {
          mel_energies[i] = logf(mel_energies[i]);
        }
      else
        {
          mel_energies[i] = -100.0f;
        }
    }
}

/****************************************************************************
 * Name: compute_mfcc
 *
 * Description:
 *   Compute MFCC features from audio frame.
 *
 ****************************************************************************/

static void compute_mfcc(struct recognition_ctx_s *ctx,
                         const audio_frame_t *frame,
                         feature_vector_t *features)
{
  float energy = 0.0f;
  int i;

  /* Apply pre-emphasis and windowing */

  for (i = 0; i < AUDIO_FRAME_SIZE; i++)
    {
      float sample = (float)frame->data[i] / 32768.0f;

      /* Pre-emphasis */

      if (i > 0)
        {
          sample -= PRE_EMPHASIS_COEFF *
                    ((float)frame->data[i - 1] / 32768.0f);
        }

      /* Hamming window */

      ctx->window[i] = sample *
                        (0.54f - 0.46f *
                         cosf(2.0f * M_PI * i / (AUDIO_FRAME_SIZE - 1)));

      energy += ctx->window[i] * ctx->window[i];
    }

  features->energy = energy;

  /* Apply DFT (Discrete Fourier Transform) */

  memset(ctx->fft_real, 0, sizeof(ctx->fft_real));
  memset(ctx->fft_imag, 0, sizeof(ctx->fft_imag));

  for (i = 0; i < FEATURE_FFT_SIZE; i++)
    {
      for (int k = 0; k < AUDIO_FRAME_SIZE; k++)
        {
          float angle = 2.0f * M_PI * i * k / FEATURE_FFT_SIZE;
          ctx->fft_real[i] += ctx->window[k] * cosf(angle);
          ctx->fft_imag[i] -= ctx->window[k] * sinf(angle);
        }
    }

  /* Compute power spectrum (only first half due to symmetry) */

  float power_spectrum[FEATURE_FFT_SIZE / 2];

  for (i = 0; i < FEATURE_FFT_SIZE / 2; i++)
    {
      power_spectrum[i] = (ctx->fft_real[i] * ctx->fft_real[i] +
                           ctx->fft_imag[i] * ctx->fft_imag[i]) /
                          (FEATURE_FFT_SIZE * FEATURE_FFT_SIZE);
    }

  /* Compute mel filterbank energies */

  compute_mel_filterbank(ctx, power_spectrum, ctx->mel_energies);

  /* Apply DCT to get MFCC (simplified) */

  for (i = 0; i < FEATURE_MFCC_COEFFS; i++)
    {
      float sum = 0.0f;
      int j;

      for (j = 0; j < FEATURE_MEL_BANDS; j++)
        {
          sum += ctx->mel_energies[j] *
                 cosf(M_PI * i * (2.0f * j + 1.0f) /
                      (2.0f * FEATURE_MEL_BANDS));
        }

      features->mfcc[i] = sum;
    }

  features->timestamp = frame->timestamp;
}

/****************************************************************************
 * Name: classify_sound
 *
 * Description:
 *   Classify the sound based on features.
 *
 ****************************************************************************/

static int classify_sound(struct recognition_ctx_s *ctx,
                          const feature_vector_t *features,
                          sound_event_t *event)
{
  float scores[SOUND_TYPE_MAX];
  float max_score = -1e10f;
  int max_type = SOUND_TYPE_UNKNOWN;
  int i, j;

  /* Compute scores for each sound type (simple linear classifier) */

  for (i = 0; i < SOUND_TYPE_MAX; i++)
    {
      scores[i] = ctx->model_bias[i];

      for (j = 0; j < FEATURE_MFCC_COEFFS; j++)
        {
          scores[i] += ctx->model_weights[i][j] * features->mfcc[j];
        }

      /* Add energy feature */

      scores[i] += features->energy * 0.001f;

      if (scores[i] > max_score)
        {
          max_score = scores[i];
          max_type = i;
        }
    }

  /* Apply softmax to get confidence */

  float sum_exp = 0.0f;
  for (i = 0; i < SOUND_TYPE_MAX; i++)
    {
      scores[i] = expf(scores[i] - max_score);
      sum_exp += scores[i];
    }

  uint8_t confidence = (uint8_t)((scores[max_type] / sum_exp) * 100.0f);

  /* Check if confidence meets threshold */

  if (confidence < ctx->threshold)
    {
      return -EAGAIN; /* No detection */
    }

  /* Fill event structure */

  event->type = max_type;
  event->confidence = confidence;
  event->timestamp = features->timestamp;

  /* Set alert level based on sound type */

  switch (max_type)
    {
      case SOUND_TYPE_FIRE_ALARM:
        event->alert_level = ALERT_LEVEL_CRITICAL;
        strncpy(event->description, "Fire Alarm", sizeof(event->description) - 1);
        event->description[sizeof(event->description) - 1] = '\0';
        break;

      case SOUND_TYPE_CAR_HORN:
        event->alert_level = ALERT_LEVEL_HIGH;
        strncpy(event->description, "Car Horn", sizeof(event->description) - 1);
        event->description[sizeof(event->description) - 1] = '\0';
        break;

      case SOUND_TYPE_DOORBELL:
        event->alert_level = ALERT_LEVEL_MEDIUM;
        strncpy(event->description, "Doorbell", sizeof(event->description) - 1);
        event->description[sizeof(event->description) - 1] = '\0';
        break;

      case SOUND_TYPE_HUMAN_CALL:
        event->alert_level = ALERT_LEVEL_MEDIUM;
        strncpy(event->description, "Human Call", sizeof(event->description) - 1);
        event->description[sizeof(event->description) - 1] = '\0';
        break;

      default:
        event->alert_level = ALERT_LEVEL_LOW;
        strncpy(event->description, "Unknown", sizeof(event->description) - 1);
        event->description[sizeof(event->description) - 1] = '\0';
        break;
    }

  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: recognition_init
 *
 * Description:
 *   Initialize the recognition module.
 *
 * Output Parameters:
 *   handle - Recognition module handle
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int recognition_init(recognition_handle_t *handle)
{
  struct recognition_ctx_s *ctx;
  int i, j;

  DEBUGASSERT(handle != NULL);

  /* Allocate context */

  ctx = (struct recognition_ctx_s *)malloc(sizeof(struct recognition_ctx_s));
  if (ctx == NULL)
    {
      syslog(LOG_ERR, "Recognition: malloc failed\n");
      return -ENOMEM;
    }

  memset(ctx, 0, sizeof(struct recognition_ctx_s));

  /* Set default configuration */

  ctx->threshold = SOUNDWATCH_CONFIDENCE_THRESHOLD;
  ctx->confirm_frames = SOUNDWATCH_CONFIRM_FRAMES;
  ctx->cooldown_ms = SOUNDWATCH_COOLDOWN_MS;
  ctx->consecutive_count = 0;
  ctx->last_type = SOUND_TYPE_UNKNOWN;
  ctx->last_timestamp = 0;

  /* Initialize model weights with pre-trained values */

  for (i = 0; i < SOUND_TYPE_MAX; i++)
    {
      ctx->model_bias[i] = 0.0f;
    }

  /* Fire alarm model - emphasizes low frequency components */

  ctx->model_bias[SOUND_TYPE_FIRE_ALARM] = 0.5f;
  ctx->model_weights[SOUND_TYPE_FIRE_ALARM][0] = 0.8f;
  ctx->model_weights[SOUND_TYPE_FIRE_ALARM][1] = 0.6f;
  ctx->model_weights[SOUND_TYPE_FIRE_ALARM][2] = 0.4f;

  /* Car horn model - emphasizes mid-low frequency components */

  ctx->model_bias[SOUND_TYPE_CAR_HORN] = 0.3f;
  ctx->model_weights[SOUND_TYPE_CAR_HORN][0] = 0.5f;
  ctx->model_weights[SOUND_TYPE_CAR_HORN][1] = 0.7f;
  ctx->model_weights[SOUND_TYPE_CAR_HORN][2] = 0.3f;

  /* Doorbell model - emphasizes mid frequency components */

  ctx->model_bias[SOUND_TYPE_DOORBELL] = 0.2f;
  ctx->model_weights[SOUND_TYPE_DOORBELL][0] = 0.3f;
  ctx->model_weights[SOUND_TYPE_DOORBELL][1] = 0.4f;
  ctx->model_weights[SOUND_TYPE_DOORBELL][2] = 0.6f;

  /* Human call model - emphasizes mid-high frequency components */

  ctx->model_bias[SOUND_TYPE_HUMAN_CALL] = 0.4f;
  ctx->model_weights[SOUND_TYPE_HUMAN_CALL][0] = 0.2f;
  ctx->model_weights[SOUND_TYPE_HUMAN_CALL][1] = 0.5f;
  ctx->model_weights[SOUND_TYPE_HUMAN_CALL][2] = 0.8f;

  *handle = (recognition_handle_t)ctx;

  syslog(LOG_INFO, "Recognition: Initialized successfully\n");
  return 0;
}

/****************************************************************************
 * Name: recognition_deinit
 *
 * Description:
 *   Deinitialize the recognition module.
 *
 * Input Parameters:
 *   handle - Recognition module handle
 *
 ****************************************************************************/

void recognition_deinit(recognition_handle_t handle)
{
  struct recognition_ctx_s *ctx = (struct recognition_ctx_s *)handle;

  if (ctx == NULL)
    {
      return;
    }

  free(ctx);

  syslog(LOG_INFO, "Recognition: Deinitialized\n");
}

/****************************************************************************
 * Name: recognition_process
 *
 * Description:
 *   Process an audio frame and detect sound events.
 *
 * Input Parameters:
 *   handle - Recognition module handle
 *   frame  - Audio frame to process
 *   event  - Buffer to store detected event
 *
 * Returned Value:
 *   0 on event detection, -EAGAIN if no event, negative errno on failure
 *
 ****************************************************************************/

int recognition_process(recognition_handle_t handle,
                        const audio_frame_t *frame,
                        sound_event_t *event)
{
  struct recognition_ctx_s *ctx = (struct recognition_ctx_s *)handle;
  feature_vector_t features;
  sound_event_t detected;
  int ret;

  DEBUGASSERT(ctx != NULL);
  DEBUGASSERT(frame != NULL);
  DEBUGASSERT(event != NULL);

  /* Check cooldown */

  if (ctx->last_timestamp > 0)
    {
      uint32_t elapsed = frame->timestamp - ctx->last_timestamp;

      if (elapsed < ctx->cooldown_ms)
        {
          return -EAGAIN;
        }
    }

  /* Compute features */

  compute_mfcc(ctx, frame, &features);

  /* Check energy threshold */

  if (features.energy < FRAME_ENERGY_THRESHOLD)
    {
      ctx->consecutive_count = 0;
      return -EAGAIN;
    }

  /* Classify the sound */

  ret = classify_sound(ctx, &features, &detected);
  if (ret < 0)
    {
      ctx->consecutive_count = 0;
      return ret;
    }

  /* Check for consecutive detections */

  if (detected.type == ctx->last_type)
    {
      ctx->consecutive_count++;
    }
  else
    {
      ctx->consecutive_count = 1;
      ctx->last_type = detected.type;
    }

  /* Require consecutive detections for confirmation */

  if (ctx->consecutive_count < ctx->confirm_frames)
    {
      return -EAGAIN;
    }

  /* Event confirmed */

  ctx->last_timestamp = frame->timestamp;
  ctx->consecutive_count = 0;

  memcpy(event, &detected, sizeof(sound_event_t));

  syslog(LOG_INFO, "Recognition: Detected type=%d confidence=%d\n",
         event->type, event->confidence);

  return 0;
}

/****************************************************************************
 * Name: recognition_set_threshold
 *
 * Description:
 *   Set the confidence threshold.
 *
 * Input Parameters:
 *   handle    - Recognition module handle
 *   threshold - New threshold value (0-100)
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int recognition_set_threshold(recognition_handle_t handle, uint8_t threshold)
{
  struct recognition_ctx_s *ctx = (struct recognition_ctx_s *)handle;

  DEBUGASSERT(ctx != NULL);

  if (threshold > 100)
    {
      return -EINVAL;
    }

  ctx->threshold = threshold;

  syslog(LOG_INFO, "Recognition: Threshold set to %d\n", threshold);
  return 0;
}
