/* Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <stdint.h>
#include <sys/param.h>
#include <sys/socket.h>
#include <syslog.h>
#include <time.h>

#include "byte_buffer.h"
#include "cras_a2dp_info.h"
#include "cras_a2dp_iodev.h"
#include "cras_audio_area.h"
#include "cras_bt_device.h"
#include "cras_iodev.h"
#include "cras_iodev_list.h"
#include "cras_util.h"
#include "rtp.h"
#include "utlist.h"

#define PCM_BUF_MAX_SIZE_BYTES 4096 /* 1024 frames of 16 bit stereo */

struct a2dp_io {
	struct cras_iodev base;
	struct a2dp_info a2dp;
	struct cras_bt_transport *transport;
	a2dp_force_suspend_cb force_suspend_cb;

	/* To hold the pcm samples. */
	struct byte_buffer *pcm_buf;


	/* Accumulated frames written to a2dp socket. Will need this info
	 * together with the device open time stamp to get how many virtual
	 * buffer is queued there.
	 */
	uint64_t bt_written_frames;
	struct timespec dev_open_time;
};

static int update_supported_formats(struct cras_iodev *iodev)
{
	struct a2dp_io *a2dpio = (struct a2dp_io *)iodev;
	size_t rate = 0;
	size_t channel;
	a2dp_sbc_t a2dp;

	cras_bt_transport_configuration(a2dpio->transport, &a2dp,
					sizeof(a2dp));

	iodev->format->format = SND_PCM_FORMAT_S16_LE;
	channel = (a2dp.channel_mode == SBC_CHANNEL_MODE_MONO) ? 1 : 2;

	if (a2dp.frequency & SBC_SAMPLING_FREQ_48000)
		rate = 48000;
	else if (a2dp.frequency & SBC_SAMPLING_FREQ_44100)
		rate = 44100;
	else if (a2dp.frequency & SBC_SAMPLING_FREQ_32000)
		rate = 32000;
	else if (a2dp.frequency & SBC_SAMPLING_FREQ_16000)
		rate = 16000;

	free(iodev->supported_rates);
	iodev->supported_rates = (size_t *)malloc(2 * sizeof(rate));
	iodev->supported_rates[0] = rate;
	iodev->supported_rates[1] = 0;

	free(iodev->supported_channel_counts);
	iodev->supported_channel_counts = (size_t *)malloc(2 * sizeof(channel));
	iodev->supported_channel_counts[0] = channel;
	iodev->supported_channel_counts[1] = 0;

	return 0;
}

/* Calculates the amount of consumed frames since given time.
 */
static uint64_t frames_since(struct timespec ts, size_t rate)
{
	struct timespec te, diff;

	clock_gettime(CLOCK_MONOTONIC, &te);
	subtract_timespecs(&te, &ts, &diff);
	return (uint64_t)diff.tv_sec * rate +
			diff.tv_nsec / (1000000000L / rate);
}

/* Calculates the number of virtual buffer in frames. Assuming all written
 * buffer is consumed in a constant frame rate at bluetooth device side.
 * Args:
 *    iodev: The a2dp iodev to estimate the queued frames for.
 *    fr: The amount of frames just transmitted.
 */
static int bt_queued_frames(const struct cras_iodev *iodev, int fr)
{
	uint64_t consumed;
	struct a2dp_io *a2dpio = (struct a2dp_io *)iodev;

	/* Calculate consumed frames since device has opened */
	a2dpio->bt_written_frames += fr;
	consumed = frames_since(a2dpio->dev_open_time,
				iodev->format->frame_rate);

	if (a2dpio->bt_written_frames > consumed)
		return a2dpio->bt_written_frames - consumed;
	else
		return 0;
}


static int frames_queued(const struct cras_iodev *iodev)
{
	size_t format_bytes;
	int frames;
	struct a2dp_io *a2dpio = (struct a2dp_io *)iodev;

	format_bytes = cras_get_format_bytes(iodev->format);

	frames = buf_queued_bytes(a2dpio->pcm_buf) / format_bytes
			+ a2dp_queued_frames(&a2dpio->a2dp)
			+ bt_queued_frames(iodev, 0);

	return frames;
}

static int open_dev(struct cras_iodev *iodev)
{
	int err = 0;
	struct a2dp_io *a2dpio = (struct a2dp_io *)iodev;

	err = cras_bt_transport_acquire(a2dpio->transport);
	if (err < 0) {
		syslog(LOG_ERR, "transport_acquire failed");
		return err;
	}

	/* Assert format is set before opening device. */
	if (iodev->format == NULL)
		return -EINVAL;
	iodev->format->format = SND_PCM_FORMAT_S16_LE;
	cras_iodev_init_audio_area(iodev, iodev->format->num_channels);

	a2dpio->pcm_buf = byte_buffer_create(PCM_BUF_MAX_SIZE_BYTES);
	if (!a2dpio->pcm_buf)
		return -ENOMEM;

	iodev->buffer_size = PCM_BUF_MAX_SIZE_BYTES
			/ cras_get_format_bytes(iodev->format);

	syslog(LOG_ERR, "a2dp iodev buf size %lu", iodev->buffer_size);

	/* Initialize variables for bt_queued_frames() */
	a2dpio->bt_written_frames = 0;
	clock_gettime(CLOCK_MONOTONIC, &a2dpio->dev_open_time);

	return 0;
}

static int close_dev(struct cras_iodev *iodev)
{
	int err;
	struct a2dp_io *a2dpio = (struct a2dp_io *)iodev;

	if (!a2dpio->transport)
		return 0;

	err = cras_bt_transport_release(a2dpio->transport);
	if (err < 0)
		syslog(LOG_ERR, "transport_release failed");

	a2dp_drain(&a2dpio->a2dp);
	byte_buffer_destroy(a2dpio->pcm_buf);
	cras_iodev_free_format(iodev);
	cras_iodev_free_audio_area(iodev);
	return 0;
}

static int is_open(const struct cras_iodev *iodev)
{
	struct a2dp_io *a2dpio = (struct a2dp_io *)iodev;
	return cras_bt_transport_fd(a2dpio->transport) > 0;
}

/* Flushes queued buffer, including pcm and a2dp buffer.
 * Returns:
 *    0 when the flush succeeded, -1 when error occurred.
 */
static int flush_data(const struct cras_iodev *iodev)
{
	int processed;
	size_t format_bytes;
	int written = 0;
	struct a2dp_io *a2dpio;

	a2dpio = (struct a2dp_io *)iodev;
	format_bytes = cras_get_format_bytes(iodev->format);

	while (buf_queued_bytes(a2dpio->pcm_buf)) {
		processed = a2dp_write(
				buf_read_pointer(a2dpio->pcm_buf),
				buf_readable_bytes(a2dpio->pcm_buf),
				&a2dpio->a2dp,
				format_bytes,
				cras_bt_transport_fd(a2dpio->transport),
				cras_bt_transport_write_mtu(a2dpio->transport),
				&written);

		if (processed < 0)
			return processed;
		if (processed == 0)
			break;
		buf_increment_read(a2dpio->pcm_buf, processed);

		if (written == -EAGAIN) {
			return 0;
		} else if (written == -ENOTCONN) {
			if (a2dpio->force_suspend_cb)
				a2dpio->force_suspend_cb(&a2dpio->base);
			return -1;
		} else if (written < 0) {
			syslog(LOG_ERR, "a2dpio write error %d", written);
			return written;
		}

		bt_queued_frames(iodev,
				 a2dp_block_size(&a2dpio->a2dp, written)
				 / format_bytes);
	}

	return 0;
}

static int dev_running(const struct cras_iodev *iodev)
{
	int err;

	if (!is_open(iodev))
		return 0;

	// Flush queued buffer when dev is open.
	err = flush_data(iodev);

	return !err;
}

static int delay_frames(const struct cras_iodev *iodev)
{
	return frames_queued(iodev);
}

static int get_buffer(struct cras_iodev *iodev,
		      struct cras_audio_area **area,
		      unsigned *frames)
{
	size_t format_bytes;
	struct a2dp_io *a2dpio;

	a2dpio = (struct a2dp_io *)iodev;

	format_bytes = cras_get_format_bytes(iodev->format);

	if (iodev->direction != CRAS_STREAM_OUTPUT)
		return 0;

	*frames = MIN(*frames, buf_writable_bytes(a2dpio->pcm_buf) /
					format_bytes);
	iodev->area->frames = *frames;
	cras_audio_area_config_buf_pointers(
			iodev->area, iodev->format,
			buf_write_pointer(a2dpio->pcm_buf));
	*area = iodev->area;
	return 0;
}

static int put_buffer(struct cras_iodev *iodev, unsigned nwritten)
{
	size_t format_bytes;
	struct a2dp_io *a2dpio = (struct a2dp_io *)iodev;

	format_bytes = cras_get_format_bytes(iodev->format);

	if (nwritten * format_bytes > buf_writable_bytes(a2dpio->pcm_buf))
		return -EINVAL;

	buf_increment_write(a2dpio->pcm_buf, nwritten * format_bytes);

	return flush_data(iodev);
}

static void update_active_node(struct cras_iodev *iodev)
{
}

void free_resources(struct a2dp_io *a2dpio)
{
	struct cras_ionode *node;

	node = a2dpio->base.active_node;
	if (node) {
		cras_iodev_rm_node(&a2dpio->base, node);
		free(node);
	}
	free(a2dpio->base.supported_channel_counts);
	free(a2dpio->base.supported_rates);
	destroy_a2dp(&a2dpio->a2dp);
}

struct cras_iodev *a2dp_iodev_create(struct cras_bt_transport *transport,
				     a2dp_force_suspend_cb force_suspend_cb)
{
	int err;
	struct a2dp_io *a2dpio;
	struct cras_iodev *iodev;
	struct cras_ionode *node;
	a2dp_sbc_t a2dp;
	struct cras_bt_device *device;

	a2dpio = (struct a2dp_io *)calloc(1, sizeof(*a2dpio));
	if (!a2dpio)
		goto error;

	a2dpio->transport = transport;
	cras_bt_transport_configuration(a2dpio->transport, &a2dp,
					sizeof(a2dp));
	err = init_a2dp(&a2dpio->a2dp, &a2dp);
	if (err) {
		syslog(LOG_ERR, "Fail to init a2dp");
		goto error;
	}
	a2dpio->force_suspend_cb = force_suspend_cb;

	iodev = &a2dpio->base;

	/* A2DP only does output now */
	iodev->direction = CRAS_STREAM_OUTPUT;

	/* Set iodev's name by bluetooth device's readable name, if
	 * the readable name is not available, use address instead.
	 */
	device = cras_bt_transport_device(transport);
	if (device)
		snprintf(iodev->info.name, sizeof(iodev->info.name), "%s",
				cras_bt_device_name(device));
	else
		snprintf(iodev->info.name, sizeof(iodev->info.name), "%s",
			 cras_bt_transport_object_path(a2dpio->transport));

	iodev->info.name[ARRAY_SIZE(iodev->info.name) - 1] = '\0';

	iodev->open_dev = open_dev;
	iodev->is_open = is_open; /* Needed by thread_add_stream */
	iodev->frames_queued = frames_queued;
	iodev->dev_running = dev_running;
	iodev->delay_frames = delay_frames;
	iodev->get_buffer = get_buffer;
	iodev->put_buffer = put_buffer;
	iodev->close_dev = close_dev;
	iodev->update_supported_formats = update_supported_formats;
	iodev->update_active_node = update_active_node;
	iodev->software_volume_needed = 1;
	iodev->software_volume_scaler = 1.0;

	/* Create a dummy ionode */
	node = (struct cras_ionode *)calloc(1, sizeof(*node));
	node->dev = iodev;
	strcpy(node->name, iodev->info.name);
	node->plugged = 1;
	node->priority = 3;
	node->type = CRAS_NODE_TYPE_BLUETOOTH;
	node->volume = 100;
	gettimeofday(&node->plugged_time, NULL);

	/* A2DP does output only */
	err = cras_iodev_list_add_output(iodev);
	if (err)
		goto error;

	cras_iodev_add_node(iodev, node);
	cras_iodev_set_active_node(iodev, node);

	return iodev;
error:
	if (a2dpio) {
		free_resources(a2dpio);
		free(a2dpio);
	}
	return NULL;
}

void a2dp_iodev_destroy(struct cras_iodev *iodev)
{
	int rc;
	struct a2dp_io *a2dpio = (struct a2dp_io *)iodev;

	/* A2DP does output only */
	rc = cras_iodev_list_rm_output(iodev);
	if (rc == -EBUSY) {
		syslog(LOG_ERR, "Failed to remove iodev %s", iodev->info.name);
		return;
	}

	/* Free resources when device successfully removed. */
	free_resources(a2dpio);
	cras_iodev_free_dsp(iodev);
	free(a2dpio);
}
