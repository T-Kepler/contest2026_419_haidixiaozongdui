/****************************************************************************
 * apps/soundwatch/components/storage/storage.c
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
#include <sys/stat.h>
#include <time.h>

#include "soundwatch.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define STORAGE_FILE_PATH      "/var/soundwatch/events.dat"
#define STORAGE_MAX_EVENTS     SOUNDWATCH_MAX_EVENTS
#define STORAGE_MAGIC          0x53574556  /* "SWEV" */

/****************************************************************************
 * Private Types
 ****************************************************************************/

/* Storage file header */

typedef struct storage_header_s
{
  uint32_t magic;          /* Magic number */
  uint32_t version;        /* File version */
  uint32_t count;          /* Number of events */
  uint32_t next_index;     /* Next write index */
  uint32_t reserved[4];    /* Reserved for future use */
} storage_header_t;

/* Storage context */

struct storage_ctx_s
{
  int      fd;              /* File descriptor */
  uint32_t count;           /* Current event count */
  uint32_t next_index;      /* Next write index */
  sound_event_t events[STORAGE_MAX_EVENTS]; /* Event buffer */
};

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int storage_load(struct storage_ctx_s *ctx);
static int storage_save(struct storage_ctx_s *ctx);
static int storage_create_file(struct storage_ctx_s *ctx);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: storage_load
 *
 * Description:
 *   Load events from storage file.
 *
 * Input Parameters:
 *   ctx - Storage context
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

static int storage_load(struct storage_ctx_s *ctx)
{
  storage_header_t header;
  ssize_t bytes_read;

  DEBUGASSERT(ctx != NULL);

  /* Read header */

  lseek(ctx->fd, 0, SEEK_SET);
  bytes_read = read(ctx->fd, &header, sizeof(header));
  if (bytes_read != sizeof(header))
    {
      syslog(LOG_ERR, "Storage: Read header failed\n");
      return -EIO;
    }

  /* Verify magic number */

  if (header.magic != STORAGE_MAGIC)
    {
      syslog(LOG_ERR, "Storage: Invalid magic number\n");
      return -EINVAL;
    }

  /* Read events */

  if (header.count > 0)
    {
      size_t events_size = header.count * sizeof(sound_event_t);

      bytes_read = read(ctx->fd, ctx->events, events_size);
      if (bytes_read != events_size)
        {
          syslog(LOG_ERR, "Storage: Read events failed\n");
          return -EIO;
        }
    }

  ctx->count = header.count;
  ctx->next_index = header.next_index;

  syslog(LOG_INFO, "Storage: Loaded %lu events\n", (unsigned long)ctx->count);
  return 0;
}

/****************************************************************************
 * Name: storage_save
 *
 * Description:
 *   Save events to storage file.
 *
 * Input Parameters:
 *   ctx - Storage context
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

static int storage_save(struct storage_ctx_s *ctx)
{
  storage_header_t header;
  ssize_t bytes_written;

  DEBUGASSERT(ctx != NULL);

  /* Prepare header */

  header.magic = STORAGE_MAGIC;
  header.version = 1;
  header.count = ctx->count;
  header.next_index = ctx->next_index;
  memset(header.reserved, 0, sizeof(header.reserved));

  /* Write header */

  lseek(ctx->fd, 0, SEEK_SET);
  bytes_written = write(ctx->fd, &header, sizeof(header));
  if (bytes_written != sizeof(header))
    {
      syslog(LOG_ERR, "Storage: Write header failed\n");
      return -EIO;
    }

  /* Write events */

  if (ctx->count > 0)
    {
      size_t events_size = ctx->count * sizeof(sound_event_t);

      bytes_written = write(ctx->fd, ctx->events, events_size);
      if (bytes_written != events_size)
        {
          syslog(LOG_ERR, "Storage: Write events failed\n");
          return -EIO;
        }
    }

  /* Flush to disk */

  fsync(ctx->fd);

  syslog(LOG_INFO, "Storage: Saved %lu events\n", (unsigned long)ctx->count);
  return 0;
}

/****************************************************************************
 * Name: storage_create_file
 *
 * Description:
 *   Create a new storage file.
 *
 * Input Parameters:
 *   ctx - Storage context
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

static int storage_create_file(struct storage_ctx_s *ctx)
{
  storage_header_t header;
  ssize_t bytes_written;

  DEBUGASSERT(ctx != NULL);

  /* Prepare header */

  header.magic = STORAGE_MAGIC;
  header.version = 1;
  header.count = 0;
  header.next_index = 0;
  memset(header.reserved, 0, sizeof(header.reserved));

  /* Write header */

  lseek(ctx->fd, 0, SEEK_SET);
  bytes_written = write(ctx->fd, &header, sizeof(header));
  if (bytes_written != sizeof(header))
    {
      syslog(LOG_ERR, "Storage: Create file failed\n");
      return -EIO;
    }

  /* Initialize context */

  ctx->count = 0;
  ctx->next_index = 0;
  memset(ctx->events, 0, sizeof(ctx->events));

  syslog(LOG_INFO, "Storage: Created new file\n");
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: storage_init
 *
 * Description:
 *   Initialize the storage module.
 *
 * Output Parameters:
 *   handle - Storage module handle
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int storage_init(storage_handle_t *handle)
{
  int ret;
  struct storage_ctx_s *ctx;

  DEBUGASSERT(handle != NULL);

  /* Allocate context */

  ctx = (struct storage_ctx_s *)malloc(sizeof(struct storage_ctx_s));
  if (ctx == NULL)
    {
      syslog(LOG_ERR, "Storage: malloc failed\n");
      return -ENOMEM;
    }

  memset(ctx, 0, sizeof(struct storage_ctx_s));

  /* Initialize file descriptor to invalid */

  ctx->fd = -1;

  /* Create directory if it doesn't exist */

  ret = mkdir("/var/soundwatch", 0755);
  if (ret < 0 && errno != EEXIST)
    {
      syslog(LOG_ERR, "Storage: mkdir failed: %d\n", errno);
      goto err_mkdir;
    }

  /* Open storage file */

  ctx->fd = open(STORAGE_FILE_PATH, O_RDWR | O_CREAT, 0644);
  if (ctx->fd < 0)
    {
      syslog(LOG_ERR, "Storage: Open file failed: %d\n", errno);
      ret = -errno;
      goto err_open;
    }

  /* Try to load existing data */

  ret = storage_load(ctx);
  if (ret < 0)
    {
      /* File may be empty or corrupted, create new */

      ret = storage_create_file(ctx);
      if (ret < 0)
        {
          goto err_create;
        }
    }

  *handle = (storage_handle_t)ctx;

  syslog(LOG_INFO, "Storage: Initialized successfully\n");
  return 0;

err_create:
  if (ctx->fd >= 0)
    {
      close(ctx->fd);
    }
err_open:
err_mkdir:
  free(ctx);
  return ret;
}

/****************************************************************************
 * Name: storage_deinit
 *
 * Description:
 *   Deinitialize the storage module.
 *
 * Input Parameters:
 *   handle - Storage module handle
 *
 ****************************************************************************/

void storage_deinit(storage_handle_t handle)
{
  struct storage_ctx_s *ctx = (struct storage_ctx_s *)handle;

  if (ctx == NULL)
    {
      return;
    }

  /* Save any pending data */

  storage_save(ctx);

  close(ctx->fd);
  free(ctx);

  syslog(LOG_INFO, "Storage: Deinitialized\n");
}

/****************************************************************************
 * Name: storage_add_event
 *
 * Description:
 *   Add a sound event to storage.
 *
 * Input Parameters:
 *   handle - Storage module handle
 *   event  - Sound event to store
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int storage_add_event(storage_handle_t handle, const sound_event_t *event)
{
  struct storage_ctx_s *ctx = (struct storage_ctx_s *)handle;
  int ret;

  DEBUGASSERT(ctx != NULL);
  DEBUGASSERT(event != NULL);

  /* Add event to buffer */

  memcpy(&ctx->events[ctx->next_index], event, sizeof(sound_event_t));

  /* Update indices */

  ctx->next_index = (ctx->next_index + 1) % STORAGE_MAX_EVENTS;
  if (ctx->count < STORAGE_MAX_EVENTS)
    {
      ctx->count++;
    }

  /* Save to file */

  ret = storage_save(ctx);
  if (ret < 0)
    {
      syslog(LOG_ERR, "Storage: Save failed: %d\n", ret);
      return ret;
    }

  syslog(LOG_INFO, "Storage: Added event type=%d\n", event->type);
  return 0;
}

/****************************************************************************
 * Name: storage_get_events
 *
 * Description:
 *   Get stored events.
 *
 * Input Parameters:
 *   handle    - Storage module handle
 *   events    - Buffer to store events
 *   max_events - Maximum number of events to retrieve
 *   count     - Actual number of events retrieved
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int storage_get_events(storage_handle_t handle, sound_event_t *events,
                       int max_events, int *count)
{
  struct storage_ctx_s *ctx = (struct storage_ctx_s *)handle;
  int i;
  int idx;

  DEBUGASSERT(ctx != NULL);
  DEBUGASSERT(events != NULL);
  DEBUGASSERT(count != NULL);

  *count = 0;

  /* Copy events in chronological order */

  for (i = 0; i < ctx->count && i < max_events; i++)
    {
      idx = (ctx->next_index - ctx->count + i + STORAGE_MAX_EVENTS) %
            STORAGE_MAX_EVENTS;
      memcpy(&events[i], &ctx->events[idx], sizeof(sound_event_t));
      (*count)++;
    }

  return 0;
}

/****************************************************************************
 * Name: storage_clear_events
 *
 * Description:
 *   Clear all stored events.
 *
 * Input Parameters:
 *   handle - Storage module handle
 *
 * Returned Value:
 *   0 on success, negative errno on failure
 *
 ****************************************************************************/

int storage_clear_events(storage_handle_t handle)
{
  struct storage_ctx_s *ctx = (struct storage_ctx_s *)handle;
  int ret;

  DEBUGASSERT(ctx != NULL);

  /* Clear buffer */

  ctx->count = 0;
  ctx->next_index = 0;
  memset(ctx->events, 0, sizeof(ctx->events));

  /* Save to file */

  ret = storage_save(ctx);
  if (ret < 0)
    {
      syslog(LOG_ERR, "Storage: Clear failed: %d\n", ret);
      return ret;
    }

  syslog(LOG_INFO, "Storage: Cleared all events\n");
  return 0;
}
