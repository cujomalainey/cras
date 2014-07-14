/* Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * The dev_stream structure is used for mapping streams to a device.  In
 * addition to the rstream, other mixing information is stored here.
 */

#ifndef DEV_STREAM_H_
#define DEV_STREAM_H_

#include <stdint.h>

#include "cras_types.h"

struct cras_audio_area;
struct cras_fmt_conv;
struct cras_rstream;

/*
 * Linked list of streams of audio from/to a client.
 * Args:
 *    stream - The rstream attached to a device.
 *    conv - Sample rate or format converter.
 *    conv_buffer - The buffer for converter if needed.
 *    conv_buffer_size_frames - Size of conv_buffer in frames.
 *    skip_mix - Don't mix this next time streams are mixed.
 *    mix_offset - Current mix progress in the buffer.
 */
struct dev_stream {
	struct cras_rstream *stream;
	struct cras_fmt_conv *conv;
	struct byte_buffer *conv_buffer;
	struct cras_audio_area *conv_area;
	unsigned int conv_buffer_size_frames;
	unsigned int skip_mix;
	unsigned int mix_offset;
	struct dev_stream *prev, *next;
};

struct dev_stream *dev_stream_create(struct cras_rstream *stream,
				     const struct cras_audio_format *dev_fmt);
void dev_stream_destroy(struct dev_stream *dev_stream);

/*
 * Renders count frames from shm into dst.  Updates count if anything is
 * written. If it's muted and the only stream zero memory.
 * Args:
 *    dev_stream - The struct holding the stream to mix.
 *    num_channels - The number of channels on the device.
 *    dst - The destination buffer for mixing.
 *    count - The number of frames written.
 *    index - The index of the stream writing to this buffer.
 */
unsigned int dev_stream_mix(struct dev_stream *dev_stream,
			    size_t num_channels,
			    uint8_t *dst,
			    size_t *count,
			    size_t *index);

/*
 * Reads froms from the source into the dev_stream.
 * Args:
 *    dev_stream - The struct holding the stream to mix to.
 *    area - The area to copy audio from.
 *    index - The index of the buffer to copy to the dev stream.
 */
void dev_stream_capture(struct dev_stream *dev_stream,
			const struct cras_audio_area *area,
			unsigned int dev_index);

/*
 * Returns the number of playback frames queued in shared memory.  This is a
 * post-format-conversion number.  If the stream is 24k with 10 frames queued
 * and the device is playing at 48k, 20 will be returned.
 */
int dev_stream_playback_frames(const struct dev_stream *dev_stream);

/*
 * Returns the number of frames free to be written to in a capture stream.  This
 * number is also post format conversion, similar to playback_frames above.
 */
unsigned int dev_stream_capture_avail(const struct dev_stream *dev_stream);

/*
 * Returns the number of frames that still need to be captured before this
 * capture stream is ready.  min_sleep is updated to hold the number of frames
 * needed if it is less that the current value.
 */
int dev_stream_capture_sleep_frames(struct dev_stream *dev_stream,
				    unsigned int written,
				    unsigned int *min_sleep);

/* Fill ts with the time the playback sample will be played. */
void cras_set_playback_timestamp(size_t frame_rate,
				 size_t frames,
				 struct cras_timespec *ts);

/* Fill ts with the time the capture sample was recorded. */
void cras_set_capture_timestamp(size_t frame_rate,
				size_t frames,
				struct cras_timespec *ts);

/* Fill shm ts with the time the playback sample will be played or the capture
 * sample was captured depending on the direction of the stream.
 * Args:
 *    delay_frames - The delay reproted by the device, in frames at the device's
 *      sample rate.
 */
void dev_stream_set_delay(const struct dev_stream *dev_stream,
			  unsigned int delay_frames);

#endif /* DEV_STREAM_H_ */