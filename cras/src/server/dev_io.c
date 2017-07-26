/* Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <poll.h>
#include <syslog.h>

#include "audio_thread_log.h"
#include "cras_audio_area.h"
#include "cras_iodev.h"
#include "cras_server_metrics.h"
#include "cras_rstream.h"
#include "cras_util.h"
#include "dev_stream.h"
#include "utlist.h"

#include "dev_io.h"

static const struct timespec playback_wake_fuzz_ts = {
	0, 500 * 1000 /* 500 usec. */
};

/* Reads any pending audio message from the socket. */
static void flush_old_aud_messages(struct cras_audio_shm *shm, int fd)
{
	struct audio_message msg;
	struct pollfd pollfd;
	int err;

	pollfd.fd = fd;
	pollfd.events = POLLIN;

	do {
		err = poll(&pollfd, 1, 0);
		if (pollfd.revents & POLLIN) {
			err = read(fd, &msg, sizeof(msg));
			cras_shm_set_callback_pending(shm, 0);
		}
	} while (err > 0);
}

/* Gets the master device which the stream is attached to. */
static inline
struct cras_iodev *get_master_dev(const struct dev_stream *stream)
{
	return (struct cras_iodev *)stream->stream->master_dev.dev_ptr;
}

/* Updates the estimated sample rate of open device to all attached
 * streams.
 */
static void update_estimated_rate(struct open_dev *adev)
{
	struct cras_iodev *master_dev;
	struct cras_iodev *dev = adev->dev;
	struct dev_stream *dev_stream;

	DL_FOREACH(dev->streams, dev_stream) {
		master_dev = get_master_dev(dev_stream);
		if (master_dev == NULL) {
			syslog(LOG_ERR, "Fail to find master open dev.");
			continue;
		}

		dev_stream_set_dev_rate(dev_stream,
				dev->ext_format->frame_rate,
				cras_iodev_get_est_rate_ratio(dev),
				cras_iodev_get_est_rate_ratio(master_dev),
				adev->coarse_rate_adjust);
	}
}

/* Asks any stream with room for more data. Sets the time stamp for all streams.
 * Args:
 *    adev - The output device streams are attached to.
 * Returns:
 *    0 on success, negative error on failure. If failed, can assume that all
 *    streams have been removed from the device.
 */
static int fetch_streams(struct open_dev *adev)
{
	struct dev_stream *dev_stream;
	struct cras_iodev *odev = adev->dev;
	int rc;
	int delay;

	delay = cras_iodev_delay_frames(odev);
	if (delay < 0)
		return delay;

	DL_FOREACH(adev->dev->streams, dev_stream) {
		struct cras_rstream *rstream = dev_stream->stream;
		struct cras_audio_shm *shm =
			cras_rstream_output_shm(rstream);
		int fd = cras_rstream_get_audio_fd(rstream);
		const struct timespec *next_cb_ts;
		struct timespec now;

		clock_gettime(CLOCK_MONOTONIC_RAW, &now);

		if (cras_shm_callback_pending(shm) && fd >= 0) {
			flush_old_aud_messages(shm, fd);
			cras_rstream_record_fetch_interval(dev_stream->stream,
							   &now);
		}

		if (cras_shm_get_frames(shm) < 0)
			cras_rstream_set_is_draining(rstream, 1);

		if (cras_rstream_get_is_draining(dev_stream->stream))
			continue;

		next_cb_ts = dev_stream_next_cb_ts(dev_stream);
		if (!next_cb_ts)
			continue;

		/* Check if it's time to get more data from this stream.
		 * Allow for waking up a little early. */
		add_timespecs(&now, &playback_wake_fuzz_ts);
		if (!timespec_after(&now, next_cb_ts))
			continue;

		if (!dev_stream_can_fetch(dev_stream)) {
			ATLOG(atlog, AUDIO_THREAD_STREAM_SKIP_CB,
			      cras_rstream_id(rstream),
			      shm->area->write_offset[0],
			      shm->area->write_offset[1]);
			continue;
		}

		dev_stream_set_delay(dev_stream, delay);

		ATLOG(atlog, AUDIO_THREAD_FETCH_STREAM, rstream->stream_id,
		      cras_rstream_get_cb_threshold(rstream), delay);

		rc = dev_stream_request_playback_samples(dev_stream, &now);
		if (rc < 0) {
			syslog(LOG_ERR, "fetch err: %d for %x",
			       rc, cras_rstream_id(rstream));
			cras_rstream_set_is_draining(rstream, 1);
		}
	}

	return 0;
}

/* Gets the max delay frames of open input devices. */
static int input_delay_frames(struct open_dev *adevs)
{
	struct open_dev *adev;
	int delay;
	int max_delay = 0;

	DL_FOREACH(adevs, adev) {
		if (!cras_iodev_is_open(adev->dev))
			continue;
		delay = cras_iodev_delay_frames(adev->dev);
		if (delay < 0)
			return delay;
		if (delay > max_delay)
			max_delay = delay;
	}
	return max_delay;
}

/* Gets the minimum amount of space available for writing across all streams.
 * Args:
 *    adev[in] - The device to capture from.
 *    write_limit[in] - Initial limit to number of frames to capture.
 *    limit_stream[out] - The pointer to the pointer of stream which
 *                        causes capture limit.
 *                        Output NULL if there is no stream that causes
 *                        capture limit less than the initial limit.
 */
static unsigned int get_stream_limit_set_delay(
		struct open_dev *adev,
		unsigned int write_limit,
		struct dev_stream **limit_stream)
{
	struct cras_rstream *rstream;
	struct cras_audio_shm *shm;
	struct dev_stream *stream;
	int delay;
	unsigned int avail;

	*limit_stream = NULL;

	/* TODO(dgreid) - Setting delay from last dev only. */
	delay = input_delay_frames(adev);

	DL_FOREACH(adev->dev->streams, stream) {
		rstream = stream->stream;

		shm = cras_rstream_input_shm(rstream);
		if (cras_shm_check_write_overrun(shm))
			ATLOG(atlog, AUDIO_THREAD_READ_OVERRUN,
			      adev->dev->info.idx, rstream->stream_id,
			      shm->area->num_overruns);
		dev_stream_set_delay(stream, delay);
		avail = dev_stream_capture_avail(stream);
		if (avail < write_limit) {
			write_limit = avail;
			*limit_stream = stream;
		}
	}

	return write_limit;
}

/*
 * Set wake_ts for this device to be the earliest wake up time for
 * dev_streams.
 */
static int set_input_dev_wake_ts(struct open_dev *adev)
{
	int rc;
	struct timespec level_tstamp, wake_time_out, min_ts, now;
	unsigned int curr_level, cap_limit;
	struct dev_stream *stream;
	struct dev_stream *cap_limit_stream;

	/* Limit the sleep time to 20 seconds. */
	min_ts.tv_sec = 20;
	min_ts.tv_nsec = 0;
	clock_gettime(CLOCK_MONOTONIC_RAW, &now);
	add_timespecs(&min_ts, &now);

	curr_level = cras_iodev_frames_queued(adev->dev, &level_tstamp);
	if (!timespec_is_nonzero(&level_tstamp))
		clock_gettime(CLOCK_MONOTONIC_RAW, &level_tstamp);


	cap_limit = get_stream_limit_set_delay(adev, adev->dev->buffer_size,
					       &cap_limit_stream);
	/*
	 * Loop through streams to find the earliest time audio thread
	 * should wake up.
	 */
	DL_FOREACH(adev->dev->streams, stream) {
		rc = dev_stream_wake_time(
			stream,
			curr_level,
			&level_tstamp,
			cap_limit,
			cap_limit_stream == stream,
			&wake_time_out);

		/*
		 * rc > 0 means there is no need to set wake up time for this
		 * stream.
		 */
		if (rc > 0)
			continue;

		if (rc < 0)
			return rc;

		if (timespec_after(&min_ts, &wake_time_out)) {
			min_ts = wake_time_out;
		}
	}
	adev->wake_ts = min_ts;
	return 0;
}

/* Read samples from an input device to the specified stream.
 * Args:
 *    adev - The device to capture samples from.
 * Returns 0 on success.
 */
static int capture_to_streams(struct open_dev *adev)
{
	struct cras_iodev *idev = adev->dev;
	snd_pcm_uframes_t remainder, hw_level, cap_limit;
	struct timespec hw_tstamp;
	int rc;
	struct dev_stream *cap_limit_stream;

	rc = cras_iodev_frames_queued(idev, &hw_tstamp);
	if (rc < 0)
		return rc;
	hw_level = rc;

	ATLOG(atlog, AUDIO_THREAD_READ_AUDIO_TSTAMP, idev->info.idx,
	      hw_tstamp.tv_sec, hw_tstamp.tv_nsec);
	if (timespec_is_nonzero(&hw_tstamp)) {
		if (hw_level)
			adev->input_streaming = 1;

		if (hw_level < idev->min_cb_level / 2)
			adev->coarse_rate_adjust = 1;
		else if (hw_level > idev->max_cb_level * 2)
			adev->coarse_rate_adjust = -1;
		else
			adev->coarse_rate_adjust = 0;
		if (cras_iodev_update_rate(idev, hw_level, &hw_tstamp))
			update_estimated_rate(adev);
	}

	cap_limit = get_stream_limit_set_delay(adev, hw_level,
					       &cap_limit_stream);
	remainder = MIN(hw_level, cap_limit);

	ATLOG(atlog, AUDIO_THREAD_READ_AUDIO, idev->info.idx,
	      hw_level, remainder);

	if (cras_iodev_state(idev) != CRAS_IODEV_STATE_NORMAL_RUN)
		return 0;

	while (remainder > 0) {
		struct cras_audio_area *area = NULL;
		struct dev_stream *stream;
		unsigned int nread, total_read;

		nread = remainder;

		rc = cras_iodev_get_input_buffer(idev, &area, &nread);
		if (rc < 0 || nread == 0)
			return rc;

		DL_FOREACH(adev->dev->streams, stream) {
			unsigned int this_read;
			unsigned int area_offset;

			area_offset = cras_iodev_stream_offset(idev, stream);
			this_read = dev_stream_capture(
				stream, area, area_offset,
				cras_iodev_get_software_gain_scaler(idev));

			cras_iodev_stream_written(idev, stream, this_read);
		}
		if (adev->dev->streams)
			total_read = cras_iodev_all_streams_written(idev);
		else
			total_read = nread; /* No streams, drop. */

		rc = cras_iodev_put_input_buffer(idev, total_read);
		if (rc < 0)
			return rc;
		remainder -= nread;

		if (total_read < nread)
			break;
	}

	ATLOG(atlog, AUDIO_THREAD_READ_AUDIO_DONE, remainder, 0, 0);

	return 0;
}

/* Fill the buffer with samples from the attached streams.
 * Args:
 *    odevs - The list of open output devices, provided so streams can be
 *            removed from all devices on error.
 *    adev - The device to write to.
 *    dst - The buffer to put the samples in (returned from snd_pcm_mmap_begin)
 *    write_limit - The maximum number of frames to write to dst.
 *
 * Returns:
 *    The number of frames rendered on success, a negative error code otherwise.
 *    This number of frames is the minimum of the amount of frames each stream
 *    could provide which is the maximum that can currently be rendered.
 */
static int write_streams(struct open_dev **odevs,
			 struct open_dev *adev,
			 uint8_t *dst,
			 size_t write_limit)
{
	struct cras_iodev *odev = adev->dev;
	struct dev_stream *curr;
	unsigned int max_offset = 0;
	unsigned int frame_bytes = cras_get_format_bytes(odev->ext_format);
	unsigned int num_playing = 0;
	unsigned int drain_limit = write_limit;

	/* Mix as much as we can, the minimum fill level of any stream. */
	max_offset = cras_iodev_max_stream_offset(odev);

        /* Mix as much as we can, the minimum fill level of any stream. */
	DL_FOREACH(adev->dev->streams, curr) {
		int dev_frames;

		/* If this is a single output dev stream, updates the latest
		 * number of frames for playback. */
		if (dev_stream_attached_devs(curr) == 1)
			dev_stream_update_frames(curr);

		dev_frames = dev_stream_playback_frames(curr);
		if (dev_frames < 0) {
			dev_io_remove_stream(
				odevs,
				curr->stream, NULL);
			continue;
		}
		ATLOG(atlog, AUDIO_THREAD_WRITE_STREAMS_STREAM,
		      curr->stream->stream_id, dev_frames,
		      cras_shm_callback_pending(
				cras_rstream_output_shm(curr->stream)));
		if (cras_rstream_get_is_draining(curr->stream)) {
			drain_limit = MIN((size_t)dev_frames, drain_limit);
			if (!dev_frames)
				dev_io_remove_stream(
					odevs,
					curr->stream, NULL);
		} else {
			write_limit = MIN((size_t)dev_frames, write_limit);
			num_playing++;
		}
	}

	if (!num_playing)
		write_limit = drain_limit;

	if (write_limit > max_offset)
		memset(dst + max_offset * frame_bytes, 0,
		       (write_limit - max_offset) * frame_bytes);

	ATLOG(atlog, AUDIO_THREAD_WRITE_STREAMS_MIX,
	      write_limit, max_offset, 0);

	DL_FOREACH(adev->dev->streams, curr) {
		unsigned int offset;
		int nwritten;

		offset = cras_iodev_stream_offset(odev, curr);
		if (offset >= write_limit)
			continue;
		nwritten = dev_stream_mix(curr, odev->ext_format,
					  dst + frame_bytes * offset,
					  write_limit - offset);

		if (nwritten < 0) {
			dev_io_remove_stream(odevs, curr->stream, NULL);
			continue;
		}

		cras_iodev_stream_written(odev, curr, nwritten);
	}

	write_limit = cras_iodev_all_streams_written(odev);

	ATLOG(atlog, AUDIO_THREAD_WRITE_STREAMS_MIXED, write_limit, 0, 0);

	return write_limit;
}

/* Returns 0 on success negative error on device failure. */
int write_output_samples(struct open_dev **odevs,
			 struct open_dev *adev)
{
	struct cras_iodev *odev = adev->dev;
	unsigned int hw_level;
	struct timespec hw_tstamp;
	unsigned int frames, fr_to_req;
	snd_pcm_sframes_t written;
	snd_pcm_uframes_t total_written = 0;
	int rc;
	uint8_t *dst = NULL;
	struct cras_audio_area *area = NULL;

	/* Possibly fill zeros for no_stream state and possibly transit state.
	 */
	rc = cras_iodev_prepare_output_before_write_samples(odev);
	if (rc < 0) {
		syslog(LOG_ERR, "Failed to prepare output dev for write");
		return rc;
	}

	if (cras_iodev_state(odev) != CRAS_IODEV_STATE_NORMAL_RUN)
		return 0;

	rc = cras_iodev_frames_queued(odev, &hw_tstamp);
	if (rc < 0)
		return rc;
	hw_level = rc;

	ATLOG(atlog, AUDIO_THREAD_FILL_AUDIO_TSTAMP, adev->dev->info.idx,
	      hw_tstamp.tv_sec, hw_tstamp.tv_nsec);
	if (timespec_is_nonzero(&hw_tstamp)) {
		if (hw_level < odev->min_cb_level / 2)
			adev->coarse_rate_adjust = 1;
		else if (hw_level > odev->max_cb_level * 2)
			adev->coarse_rate_adjust = -1;
		else
			adev->coarse_rate_adjust = 0;

		if (cras_iodev_update_rate(odev, hw_level, &hw_tstamp))
			update_estimated_rate(adev);
	}
	ATLOG(atlog, AUDIO_THREAD_FILL_AUDIO, adev->dev->info.idx, hw_level, 0);

	/* Don't request more than hardware can hold. Note that min_buffer_level
	 * has been subtracted from the actual hw_level so we need to take it
	 * into account here. */
	fr_to_req = cras_iodev_buffer_avail(odev, hw_level);

	/* Have to loop writing to the device, will be at most 2 loops, this
	 * only happens when the circular buffer is at the end and returns us a
	 * partial area to write to from mmap_begin */
	while (total_written < fr_to_req) {
		frames = fr_to_req - total_written;
		rc = cras_iodev_get_output_buffer(odev, &area, &frames);
		if (rc < 0)
			return rc;

		/* TODO(dgreid) - This assumes interleaved audio. */
		dst = area->channels[0].buf;
		written = write_streams(odevs, adev, dst, frames);
		if (written < 0) /* pcm has been closed */
			return (int)written;

		if (written < (snd_pcm_sframes_t)frames)
			/* Got all the samples from client that we can, but it
			 * won't fill the request. */
			fr_to_req = 0; /* break out after committing samples */

		rc = cras_iodev_put_output_buffer(odev, dst, written);
		if (rc < 0)
			return rc;
		total_written += written;
	}

	/* Empty hardware and nothing written, zero fill it if it is running. */
	if (!hw_level && !total_written &&
	    odev->min_cb_level < odev->buffer_size)
		cras_iodev_output_underrun(odev);

	ATLOG(atlog, AUDIO_THREAD_FILL_AUDIO_DONE, hw_level,
	      total_written, odev->min_cb_level);
	return 0;
}

/*
 * Public funcitons.
 */

int dev_io_send_captured_samples(struct open_dev *idev_list)
{
	struct open_dev *adev;
	int rc;

	// TODO(dgreid) - once per rstream, not once per dev_stream.
	DL_FOREACH(idev_list, adev) {
		struct dev_stream *stream;

		if (!cras_iodev_is_open(adev->dev))
			continue;

		/* Post samples to rstream if there are enough samples. */
		DL_FOREACH(adev->dev->streams, stream) {
			dev_stream_capture_update_rstream(stream);
		}

		/* Set wake_ts for this device. */
		rc = set_input_dev_wake_ts(adev);
		if (rc < 0)
			return rc;
	}

	return 0;
}

int dev_io_capture(struct open_dev **list)
{
	struct open_dev *idev_list = *list;
	struct open_dev *adev;

	DL_FOREACH(idev_list, adev) {
		if (!cras_iodev_is_open(adev->dev))
			continue;
		if (capture_to_streams(adev) < 0)
			dev_io_rm_open_dev(list, adev);
	}

	return 0;
}

void dev_io_playback_fetch(struct open_dev *odev_list)
{
	struct open_dev *adev;

	DL_FOREACH(odev_list, adev) {
		if (!cras_iodev_is_open(adev->dev))
			continue;
		fetch_streams(adev);
	}
}

int dev_io_playback_write(struct open_dev **odevs)
{
	struct open_dev *adev;
	struct dev_stream *curr;
	int rc;

	/* For multiple output case, update the number of queued frames in shm
	 * of all streams before starting write output samples. */
	adev = *odevs;
	if (adev && adev->next) {
		DL_FOREACH(*odevs, adev) {
			DL_FOREACH(adev->dev->streams, curr)
				dev_stream_update_frames(curr);
		}
	}

	DL_FOREACH(*odevs, adev) {
		if (!cras_iodev_is_open(adev->dev))
			continue;

		rc = write_output_samples(odevs, adev);
		if (rc < 0) {
			if (rc == -EPIPE) {
				/* Handle severe underrun. */
				ATLOG(atlog, AUDIO_THREAD_SEVERE_UNDERRUN,
				      adev->dev->info.idx, 0, 0);
				cras_iodev_reset_request(adev->dev);
			} else {
				/* Device error, close it. */
				dev_io_rm_open_dev(odevs, adev);
			}
		}
	}

	/* TODO(dgreid) - once per rstream, not once per dev_stream. */
	DL_FOREACH(*odevs, adev) {
		struct dev_stream *stream;
		if (!cras_iodev_is_open(adev->dev))
			continue;
		DL_FOREACH(adev->dev->streams, stream) {
			dev_stream_playback_update_rstream(stream);
		}
	}

	return 0;
}

struct open_dev *dev_io_find_open_dev(struct open_dev *odev_list,
				      const struct cras_iodev *dev)
{
	struct open_dev *odev;
	DL_FOREACH(odev_list, odev)
		if (odev->dev == dev)
			return odev;
	return NULL;
}

void dev_io_rm_open_dev(struct open_dev **odev_list,
			struct open_dev *dev_to_rm)
{
	struct open_dev *odev;
	struct dev_stream *dev_stream;

	/* Do nothing if dev_to_rm wasn't already in the active dev list. */
	odev = dev_io_find_open_dev(*odev_list, dev_to_rm->dev);
	if (!odev)
		return;

	DL_DELETE(*odev_list, dev_to_rm);

	ATLOG(atlog, AUDIO_THREAD_DEV_REMOVED, dev_to_rm->dev->info.idx, 0, 0);

	DL_FOREACH(dev_to_rm->dev->streams, dev_stream) {
		cras_iodev_rm_stream(dev_to_rm->dev, dev_stream->stream);
		dev_stream_destroy(dev_stream);
	}

	free(dev_to_rm);
}

static void delete_stream_from_dev(struct cras_iodev *dev,
				   struct cras_rstream *stream)
{
	struct dev_stream *out;

	out = cras_iodev_rm_stream(dev, stream);
	if (out)
		dev_stream_destroy(out);
}

int dev_io_remove_stream(struct open_dev **dev_list,
			 struct cras_rstream *stream,
			 struct cras_iodev *dev)
{
	struct open_dev *open_dev;
	struct timespec delay;
	unsigned fetch_delay_msec;

	/* Metrics log the longest fetch delay of this stream. */
	if (timespec_after(&stream->longest_fetch_interval,
			   &stream->sleep_interval_ts)) {
		subtract_timespecs(&stream->longest_fetch_interval,
				   &stream->sleep_interval_ts,
				   &delay);
		fetch_delay_msec = delay.tv_sec * 1000 +
				   delay.tv_nsec / 1000000;
		if (fetch_delay_msec)
			cras_server_metrics_longest_fetch_delay(
					fetch_delay_msec);
	}

	ATLOG(atlog, AUDIO_THREAD_STREAM_REMOVED, stream->stream_id, 0, 0);

	if (dev == NULL) {
		DL_FOREACH(*dev_list, open_dev) {
			delete_stream_from_dev(open_dev->dev, stream);
		}
	} else {
		delete_stream_from_dev(dev, stream);
	}

	return 0;
}
