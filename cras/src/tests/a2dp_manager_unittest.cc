// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

extern "C" {
#include "cras_a2dp_manager.h"
#include "cras_audio_format.h"
#include "cras_bt_log.h"
#include "cras_fl_media.h"
#include "cras_main_message.h"
}
static struct cras_a2dp* a2dp_pcm_iodev_create_a2dp_val;
static struct cras_iodev* a2dp_pcm_iodev_create_ret;
static struct cras_iodev* a2dp_pcm_iodev_destroy_iodev_val;
static cras_main_message* cras_main_message_send_msg;
static cras_message_callback cras_main_message_add_handler_callback;
static void* cras_main_message_add_handler_callback_data;
static int cras_tm_create_timer_called;
static int cras_tm_cancel_timer_called;
static void (*cras_tm_create_timer_cb)(struct cras_timer* t, void* data);
static void* cras_tm_create_timer_cb_data;
static struct cras_timer* cras_tm_cancel_timer_arg;
static struct cras_timer* cras_tm_create_timer_ret;
static const int fake_skt = 456;
static int floss_media_a2dp_set_active_device_called;
static int floss_media_a2dp_set_audio_config_called;
static int floss_media_a2dp_set_audio_config_rate;
static int floss_media_a2dp_set_audio_config_bps;
static int floss_media_a2dp_set_audio_config_channels;
static int floss_media_a2dp_start_audio_request_called;
static int floss_media_a2dp_stop_audio_request_called;

void ResetStubData() {
  floss_media_a2dp_set_active_device_called = 0;
  floss_media_a2dp_set_audio_config_called = 0;
  floss_media_a2dp_set_audio_config_rate = 0;
  floss_media_a2dp_set_audio_config_bps = 0;
  floss_media_a2dp_set_audio_config_channels = 0;
  floss_media_a2dp_start_audio_request_called = 0;
  floss_media_a2dp_stop_audio_request_called = 0;
}

namespace {

class A2dpManagerTestSuite : public testing::Test {
 protected:
  virtual void SetUp() {
    ResetStubData();
    btlog = cras_bt_event_log_init();
  }

  virtual void TearDown() {
    if (cras_main_message_send_msg)
      free(cras_main_message_send_msg);
    cras_bt_event_log_deinit(btlog);
  }
};

TEST_F(A2dpManagerTestSuite, CreateDestroy) {
  a2dp_pcm_iodev_create_ret = reinterpret_cast<struct cras_iodev*>(0x123);
  struct cras_a2dp* a2dp = cras_floss_a2dp_create(NULL, "addr", 1, 1, 1);
  ASSERT_NE(a2dp, (struct cras_a2dp*)NULL);
  EXPECT_EQ(a2dp, a2dp_pcm_iodev_create_a2dp_val);

  // Expect another call to a2dp create returns null.
  struct cras_a2dp* expect_null =
      cras_floss_a2dp_create(NULL, "addr2", 1, 1, 1);
  EXPECT_EQ((void*)NULL, expect_null);

  cras_floss_a2dp_destroy(a2dp);
  EXPECT_EQ(a2dp_pcm_iodev_destroy_iodev_val, a2dp_pcm_iodev_create_ret);
}

TEST_F(A2dpManagerTestSuite, StartStop) {
  struct cras_audio_format fmt;
  struct cras_a2dp* a2dp = cras_floss_a2dp_create(NULL, "addr", 1, 1, 1);
  int skt = -1;

  ASSERT_NE(a2dp, (struct cras_a2dp*)NULL);

  /* Assert the format converts to the correct bitmap as Floss defined */
  fmt.frame_rate = 44100;
  fmt.format = SND_PCM_FORMAT_S32_LE;
  fmt.num_channels = 2;
  cras_floss_a2dp_start(a2dp, &fmt, &skt);
  EXPECT_EQ(skt, fake_skt);
  EXPECT_EQ(floss_media_a2dp_set_active_device_called, 1);
  EXPECT_EQ(floss_media_a2dp_set_audio_config_called, 1);
  EXPECT_EQ(floss_media_a2dp_set_audio_config_rate, FL_RATE_44100);
  EXPECT_EQ(floss_media_a2dp_set_audio_config_bps, FL_SAMPLE_32);
  EXPECT_EQ(floss_media_a2dp_set_audio_config_channels, FL_MODE_STEREO);
  EXPECT_EQ(floss_media_a2dp_start_audio_request_called, 1);

  cras_floss_a2dp_stop(a2dp);
  EXPECT_EQ(floss_media_a2dp_stop_audio_request_called, 1);
  cras_floss_a2dp_destroy(a2dp);
}

TEST(A2dpManager, FillFormat) {
  size_t *supported_channel_counts, *supported_rates;
  snd_pcm_format_t* supported_formats;
  int num_expected_rates = 3;
  size_t expected_rates[] = {16000, 44100, 48000};
  int num_unexpected_rates = 2;
  size_t unexpected_rates[] = {96000, 192000};
  int num_expected_formats = 2;
  snd_pcm_format_t expected_formats[] = {SND_PCM_FORMAT_S16_LE,
                                         SND_PCM_FORMAT_S24_LE};
  int num_unexpected_formats = 1;
  snd_pcm_format_t unexpected_formats[] = {SND_PCM_FORMAT_S32_LE};
  int num_expected_channel_counts = 2;
  size_t expected_channel_counts[] = {1, 2};

  /* Expect Floss defined bitmap converts to supported formats array. */
  cras_floss_a2dp_fill_format(FL_RATE_44100 | FL_RATE_48000 | FL_RATE_16000,
                              FL_SAMPLE_16 | FL_SAMPLE_24,
                              FL_MODE_MONO | FL_MODE_STEREO, &supported_rates,
                              &supported_formats, &supported_channel_counts);
  for (int n = 0; n < num_expected_rates; n++) {
    int found = 0;
    for (int i = 0; supported_rates[i]; i++) {
      if (supported_rates[i] == expected_rates[n])
        found = 1;
    }
    EXPECT_EQ(found, 1);
  }
  for (int n = 0; n < num_unexpected_rates; n++) {
    int found = 0;
    for (int i = 0; supported_rates[i]; i++) {
      if (supported_rates[i] == unexpected_rates[n])
        found = 1;
    }
    EXPECT_EQ(found, 0);
  }
  for (int n = 0; n < num_expected_formats; n++) {
    int found = 0;
    for (int i = 0; supported_formats[i]; i++) {
      if (supported_formats[i] == expected_formats[n])
        found = 1;
    }
    EXPECT_EQ(found, 1);
  }
  for (int n = 0; n < num_unexpected_formats; n++) {
    int found = 0;
    for (int i = 0; supported_formats[i]; i++) {
      if (supported_formats[i] == unexpected_formats[n])
        found = 1;
    }
    EXPECT_EQ(found, 0);
  }
  for (int n = 0; n < num_expected_channel_counts; n++) {
    int found = 0;
    for (int i = 0; supported_channel_counts[i]; i++) {
      if (supported_channel_counts[i] == expected_channel_counts[n])
        found = 1;
    }
    EXPECT_EQ(found, 1);
  }
  free(supported_channel_counts);
  free(supported_rates);
  free(supported_formats);
}

}  // namespace

extern "C" {
struct cras_bt_event_log* btlog;

struct cras_iodev* a2dp_pcm_iodev_create(struct cras_a2dp* a2dp,
                                         int sample_rates,
                                         int sample_sizes,
                                         int channel_modes) {
  a2dp_pcm_iodev_create_a2dp_val = a2dp;
  return a2dp_pcm_iodev_create_ret;
}
void a2dp_pcm_iodev_destroy(struct cras_iodev* iodev) {
  a2dp_pcm_iodev_destroy_iodev_val = iodev;
}

int cras_main_message_send(struct cras_main_message* msg) {
  // cras_main_message is a local variable from caller, we should allocate
  // memory from heap and copy its data
  if (cras_main_message_send_msg)
    free(cras_main_message_send_msg);
  cras_main_message_send_msg =
      (struct cras_main_message*)calloc(1, msg->length);
  memcpy((void*)cras_main_message_send_msg, (void*)msg, msg->length);
  return 0;
}

int cras_main_message_add_handler(enum CRAS_MAIN_MESSAGE_TYPE type,
                                  cras_message_callback callback,
                                  void* callback_data) {
  cras_main_message_add_handler_callback = callback;
  cras_main_message_add_handler_callback_data = callback_data;
  return 0;
}

void cras_main_message_rm_handler(enum CRAS_MAIN_MESSAGE_TYPE type) {}

/* From cras_system_state */
struct cras_tm* cras_system_state_get_tm() {
  return NULL;
}

/* socket and connect */
int socket(int domain, int type, int protocol) {
  return fake_skt;
}
int connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen) {
  return 0;
}

/* From cras_tm */
struct cras_timer* cras_tm_create_timer(struct cras_tm* tm,
                                        unsigned int ms,
                                        void (*cb)(struct cras_timer* t,
                                                   void* data),
                                        void* cb_data) {
  cras_tm_create_timer_called++;
  cras_tm_create_timer_cb = cb;
  cras_tm_create_timer_cb_data = cb_data;
  return cras_tm_create_timer_ret;
}

void cras_tm_cancel_timer(struct cras_tm* tm, struct cras_timer* t) {
  cras_tm_cancel_timer_called++;
  cras_tm_cancel_timer_arg = t;
}

/* From fl_media */
int floss_media_a2dp_set_active_device(struct fl_media* fm, const char* addr) {
  floss_media_a2dp_set_active_device_called++;
  return 0;
}
int floss_media_a2dp_set_audio_config(struct fl_media* fm,
                                      unsigned int rate,
                                      unsigned int bps,
                                      unsigned int channels) {
  floss_media_a2dp_set_audio_config_called++;
  floss_media_a2dp_set_audio_config_rate = rate;
  floss_media_a2dp_set_audio_config_bps = bps;
  floss_media_a2dp_set_audio_config_channels = channels;
  return 0;
}

int floss_media_a2dp_start_audio_request(struct fl_media* fm) {
  floss_media_a2dp_start_audio_request_called++;
  return 0;
}

int floss_media_a2dp_stop_audio_request(struct fl_media* fm) {
  floss_media_a2dp_stop_audio_request_called++;
  return 0;
}

}  // extern "C"

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}