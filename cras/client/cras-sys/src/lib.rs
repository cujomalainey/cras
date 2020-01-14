// Copyright 2019 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
extern crate data_model;

use std::convert::{TryFrom, TryInto};
use std::error;
use std::fmt;
use std::os::raw::c_char;
use std::time::Duration;

#[allow(dead_code)]
#[allow(non_upper_case_globals)]
#[allow(non_camel_case_types)]
#[allow(non_snake_case)]
pub mod gen;
use gen::{
    _snd_pcm_format, audio_dev_debug_info, audio_message, audio_stream_debug_info,
    cras_audio_format_packed, cras_iodev_info, cras_ionode_info, cras_ionode_info__bindgen_ty_1,
    cras_timespec, CRAS_AUDIO_MESSAGE_ID, CRAS_CHANNEL, CRAS_CLIENT_TYPE, CRAS_NODE_TYPE,
    CRAS_STREAM_DIRECTION, CRAS_STREAM_EFFECT, CRAS_STREAM_TYPE,
};

unsafe impl data_model::DataInit for gen::audio_message {}
unsafe impl data_model::DataInit for gen::audio_debug_info {}
unsafe impl data_model::DataInit for gen::audio_dev_debug_info {}
unsafe impl data_model::DataInit for gen::audio_stream_debug_info {}
unsafe impl data_model::DataInit for gen::cras_client_connected {}
unsafe impl data_model::DataInit for gen::cras_client_stream_connected {}
unsafe impl data_model::DataInit for gen::cras_connect_message {}
unsafe impl data_model::DataInit for gen::cras_disconnect_stream_message {}
unsafe impl data_model::DataInit for gen::cras_dump_audio_thread {}
unsafe impl data_model::DataInit for gen::cras_iodev_info {}
unsafe impl data_model::DataInit for gen::cras_ionode_info {}
unsafe impl data_model::DataInit for gen::cras_server_state {}
unsafe impl data_model::DataInit for gen::cras_set_system_mute {}
unsafe impl data_model::DataInit for gen::cras_set_system_volume {}

/// An enumeration of errors that can occur when converting the packed C
/// structs into Rust-style structs.
#[derive(Debug)]
pub enum Error {
    InvalidChannel(i8),
    InvalidClientType(u32),
    InvalidStreamType(u32),
}

impl error::Error for Error {}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        use Error::*;
        match self {
            InvalidChannel(c) => write!(
                f,
                "Channel value {} is not within valid range [0, {})",
                c,
                CRAS_CHANNEL::CRAS_CH_MAX as u32
            ),
            InvalidClientType(t) => write!(
                f,
                "Client type {} is not within valid range [0, {})",
                t,
                CRAS_CLIENT_TYPE::CRAS_CLIENT_TYPE_SERVER_STREAM as u32 + 1
            ),
            InvalidStreamType(t) => write!(
                f,
                "Stream type {} is not within valid range [0, {})",
                t,
                CRAS_STREAM_TYPE::CRAS_STREAM_NUM_TYPES as u32
            ),
        }
    }
}

impl cras_audio_format_packed {
    /// Initializes `cras_audio_format_packed` from input parameters.
    ///
    /// # Arguments
    /// * `format` - Format in used.
    /// * `rate` - Rate in used.
    /// * `num_channels` - Number of channels in used.
    ///
    /// # Returns
    /// Structure `cras_audio_format_packed`
    pub fn new(format: _snd_pcm_format, rate: usize, num_channels: usize) -> Self {
        let mut audio_format = Self {
            format: format as i32,
            frame_rate: rate as u32,
            num_channels: num_channels as u32,
            channel_layout: [-1; CRAS_CHANNEL::CRAS_CH_MAX as usize],
        };
        for i in 0..CRAS_CHANNEL::CRAS_CH_MAX as usize {
            if i < num_channels {
                audio_format.channel_layout[i] = i as i8;
            } else {
                break;
            }
        }
        audio_format
    }
}

impl Default for audio_message {
    fn default() -> Self {
        Self {
            error: 0,
            frames: 0,
            id: CRAS_AUDIO_MESSAGE_ID::NUM_AUDIO_MESSAGES,
        }
    }
}

impl Default for cras_iodev_info {
    fn default() -> Self {
        Self {
            idx: 0,
            name: [0; 64usize],
            stable_id: 0,
        }
    }
}

#[derive(Debug)]
pub struct CrasIodevInfo {
    pub index: u32,
    pub name: String,
}

fn cstring_to_string(cstring: &[c_char]) -> String {
    let null_idx = match cstring.iter().enumerate().find(|(_, &c)| c == 0) {
        Some((i, _)) => i,
        None => return "".to_owned(),
    };

    let ptr = cstring.as_ptr() as *const u8;
    let slice = unsafe { core::slice::from_raw_parts(ptr, null_idx) };
    String::from_utf8_lossy(slice).to_string()
}

impl From<cras_iodev_info> for CrasIodevInfo {
    fn from(info: cras_iodev_info) -> Self {
        Self {
            index: info.idx,
            name: cstring_to_string(&info.name),
        }
    }
}

impl Default for cras_ionode_info {
    fn default() -> Self {
        Self {
            iodev_idx: 0,
            ionode_idx: 0,
            plugged: 0,
            active: 0,
            plugged_time: cras_ionode_info__bindgen_ty_1 {
                tv_sec: 0,
                tv_usec: 0,
            },
            volume: 0,
            capture_gain: 0,
            left_right_swapped: 0,
            type_enum: 0,
            stable_id: 0,
            mic_positions: [0; 128usize],
            type_: [0; 32usize],
            name: [0; 64usize],
            active_hotword_model: [0; 16usize],
        }
    }
}

impl From<u32> for CRAS_NODE_TYPE {
    fn from(node_type: u32) -> CRAS_NODE_TYPE {
        use CRAS_NODE_TYPE::*;
        match node_type {
            0 => CRAS_NODE_TYPE_INTERNAL_SPEAKER,
            1 => CRAS_NODE_TYPE_HEADPHONE,
            2 => CRAS_NODE_TYPE_HDMI,
            3 => CRAS_NODE_TYPE_HAPTIC,
            4 => CRAS_NODE_TYPE_LINEOUT,
            5 => CRAS_NODE_TYPE_MIC,
            6 => CRAS_NODE_TYPE_HOTWORD,
            7 => CRAS_NODE_TYPE_POST_MIX_PRE_DSP,
            8 => CRAS_NODE_TYPE_POST_DSP,
            9 => CRAS_NODE_TYPE_USB,
            10 => CRAS_NODE_TYPE_BLUETOOTH,
            _ => CRAS_NODE_TYPE_UNKNOWN,
        }
    }
}

#[derive(Debug)]
pub struct CrasIonodeInfo {
    pub name: String,
    pub iodev_index: u32,
    pub ionode_index: u32,
    pub stable_id: u32,
    pub plugged: bool,
    pub active: bool,
    pub node_type: CRAS_NODE_TYPE,
    pub type_name: String,
    pub volume: u32,
    pub capture_gain: i32,
    pub plugged_time: cras_timespec,
}

impl From<cras_ionode_info> for CrasIonodeInfo {
    fn from(info: cras_ionode_info) -> Self {
        Self {
            name: cstring_to_string(&info.name),
            iodev_index: info.iodev_idx,
            ionode_index: info.ionode_idx,
            stable_id: info.stable_id,
            plugged: info.plugged != 0,
            active: info.active != 0,
            node_type: CRAS_NODE_TYPE::from(info.type_enum),
            type_name: cstring_to_string(&info.type_),
            volume: info.volume,
            capture_gain: info.capture_gain,
            plugged_time: cras_timespec {
                tv_sec: info.plugged_time.tv_sec,
                tv_nsec: info.plugged_time.tv_usec * 1000,
            },
        }
    }
}

impl From<u32> for CRAS_STREAM_DIRECTION {
    fn from(node_type: u32) -> CRAS_STREAM_DIRECTION {
        use CRAS_STREAM_DIRECTION::*;
        match node_type {
            0 => CRAS_STREAM_OUTPUT,
            1 => CRAS_STREAM_INPUT,
            2 => CRAS_STREAM_UNDEFINED,
            3 => CRAS_STREAM_POST_MIX_PRE_DSP,
            _ => CRAS_STREAM_UNDEFINED,
        }
    }
}

impl Default for audio_dev_debug_info {
    fn default() -> Self {
        Self {
            dev_name: [0; 64],
            buffer_size: 0,
            min_buffer_level: 0,
            min_cb_level: 0,
            max_cb_level: 0,
            frame_rate: 0,
            num_channels: 0,
            est_rate_ratio: 0.0,
            direction: 0,
            num_underruns: 0,
            num_severe_underruns: 0,
            highest_hw_level: 0,
            runtime_sec: 0,
            runtime_nsec: 0,
            longest_wake_sec: 0,
            longest_wake_nsec: 0,
            software_gain_scaler: 0.0,
        }
    }
}

/// A rust-style representation of the server's packed audio_dev_debug_info
/// struct.
#[derive(Debug)]
pub struct AudioDevDebugInfo {
    pub dev_name: String,
    pub buffer_size: u32,
    pub min_buffer_level: u32,
    pub min_cb_level: u32,
    pub max_cb_level: u32,
    pub frame_rate: u32,
    pub num_channels: u32,
    pub est_rate_ratio: f64,
    pub direction: CRAS_STREAM_DIRECTION,
    pub num_underruns: u32,
    pub num_severe_underruns: u32,
    pub highest_hw_level: u32,
    pub runtime: Duration,
    pub longest_wake: Duration,
    pub software_gain_scaler: f64,
}

impl From<audio_dev_debug_info> for AudioDevDebugInfo {
    fn from(info: audio_dev_debug_info) -> Self {
        Self {
            dev_name: cstring_to_string(&info.dev_name),
            buffer_size: info.buffer_size,
            min_buffer_level: info.min_buffer_level,
            min_cb_level: info.min_cb_level,
            max_cb_level: info.max_cb_level,
            frame_rate: info.frame_rate,
            num_channels: info.num_channels,
            est_rate_ratio: info.est_rate_ratio,
            direction: CRAS_STREAM_DIRECTION::from(u32::from(info.direction)),
            num_underruns: info.num_underruns,
            num_severe_underruns: info.num_severe_underruns,
            highest_hw_level: info.highest_hw_level,
            runtime: Duration::new(info.runtime_sec.into(), info.runtime_nsec),
            longest_wake: Duration::new(info.longest_wake_sec.into(), info.longest_wake_nsec),
            software_gain_scaler: info.software_gain_scaler,
        }
    }
}

impl fmt::Display for AudioDevDebugInfo {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        writeln!(f, "Device: {}", self.dev_name)?;
        writeln!(f, "  Direction: {:?}", self.direction)?;
        writeln!(f, "  Buffer size: {}", self.buffer_size)?;
        writeln!(f, "  Minimum buffer level: {}", self.min_buffer_level)?;
        writeln!(f, "  Minimum callback level: {}", self.min_cb_level)?;
        writeln!(f, "  Max callback level: {}", self.max_cb_level)?;
        writeln!(f, "  Frame rate: {}", self.frame_rate)?;
        writeln!(f, "  Number of channels: {}", self.num_channels)?;
        writeln!(f, "  Estimated rate ratio: {:.2}", self.est_rate_ratio)?;
        writeln!(f, "  Underrun count: {}", self.num_underruns)?;
        writeln!(f, "  Severe underrun count: {}", self.num_severe_underruns)?;
        writeln!(f, "  Highest hardware level: {}", self.highest_hw_level)?;
        writeln!(f, "  Runtime: {:?}", self.runtime)?;
        writeln!(f, "  Longest wake: {:?}", self.longest_wake)?;
        writeln!(f, "  Software gain scaler: {}", self.software_gain_scaler)?;
        Ok(())
    }
}

impl TryFrom<u32> for CRAS_STREAM_TYPE {
    type Error = Error;
    fn try_from(stream_type: u32) -> Result<Self, Self::Error> {
        use CRAS_STREAM_TYPE::*;
        match stream_type {
            0 => Ok(CRAS_STREAM_TYPE_DEFAULT),
            1 => Ok(CRAS_STREAM_TYPE_MULTIMEDIA),
            2 => Ok(CRAS_STREAM_TYPE_VOICE_COMMUNICATION),
            3 => Ok(CRAS_STREAM_TYPE_SPEECH_RECOGNITION),
            4 => Ok(CRAS_STREAM_TYPE_PRO_AUDIO),
            5 => Ok(CRAS_STREAM_TYPE_ACCESSIBILITY),
            _ => Err(Error::InvalidStreamType(stream_type)),
        }
    }
}

impl TryFrom<u32> for CRAS_CLIENT_TYPE {
    type Error = Error;
    fn try_from(client_type: u32) -> Result<Self, Self::Error> {
        use CRAS_CLIENT_TYPE::*;
        match client_type {
            0 => Ok(CRAS_CLIENT_TYPE_UNKNOWN),
            1 => Ok(CRAS_CLIENT_TYPE_LEGACY),
            2 => Ok(CRAS_CLIENT_TYPE_TEST),
            3 => Ok(CRAS_CLIENT_TYPE_PCM),
            4 => Ok(CRAS_CLIENT_TYPE_CHROME),
            5 => Ok(CRAS_CLIENT_TYPE_ARC),
            6 => Ok(CRAS_CLIENT_TYPE_CROSVM),
            7 => Ok(CRAS_CLIENT_TYPE_SERVER_STREAM),
            _ => Err(Error::InvalidClientType(client_type)),
        }
    }
}

impl Default for audio_stream_debug_info {
    fn default() -> Self {
        Self {
            stream_id: 0,
            dev_idx: 0,
            direction: 0,
            stream_type: 0,
            client_type: 0,
            buffer_frames: 0,
            cb_threshold: 0,
            effects: 0,
            flags: 0,
            frame_rate: 0,
            num_channels: 0,
            longest_fetch_sec: 0,
            longest_fetch_nsec: 0,
            num_missed_cb: 0,
            num_overruns: 0,
            is_pinned: 0,
            pinned_dev_idx: 0,
            runtime_sec: 0,
            runtime_nsec: 0,
            stream_volume: 0.0,
            channel_layout: [0; 11],
        }
    }
}

impl TryFrom<i8> for CRAS_CHANNEL {
    type Error = Error;
    fn try_from(channel: i8) -> Result<Self, Self::Error> {
        use CRAS_CHANNEL::*;
        match channel {
            0 => Ok(CRAS_CH_FL),
            1 => Ok(CRAS_CH_FR),
            2 => Ok(CRAS_CH_RL),
            3 => Ok(CRAS_CH_RR),
            4 => Ok(CRAS_CH_FC),
            5 => Ok(CRAS_CH_LFE),
            6 => Ok(CRAS_CH_SL),
            7 => Ok(CRAS_CH_SR),
            8 => Ok(CRAS_CH_RC),
            9 => Ok(CRAS_CH_FLC),
            10 => Ok(CRAS_CH_FRC),
            _ => Err(Error::InvalidChannel(channel)),
        }
    }
}

/// A rust-style representation of the server's packed audio_stream_debug_info
/// struct.
#[derive(Debug)]
pub struct AudioStreamDebugInfo {
    pub stream_id: u64,
    pub dev_idx: u32,
    pub direction: CRAS_STREAM_DIRECTION,
    pub stream_type: CRAS_STREAM_TYPE,
    pub client_type: CRAS_CLIENT_TYPE,
    pub buffer_frames: u32,
    pub cb_threshold: u32,
    pub effects: u64,
    pub flags: u32,
    pub frame_rate: u32,
    pub num_channels: u32,
    pub longest_fetch: Duration,
    pub num_missed_cb: u32,
    pub num_overruns: u32,
    pub is_pinned: bool,
    pub pinned_dev_idx: u32,
    pub runtime: Duration,
    pub stream_volume: f64,
    pub channel_layout: Vec<CRAS_CHANNEL>,
}

impl TryFrom<audio_stream_debug_info> for AudioStreamDebugInfo {
    type Error = Error;
    fn try_from(info: audio_stream_debug_info) -> Result<Self, Self::Error> {
        let channel_layout = info
            .channel_layout
            .iter()
            .cloned()
            .take_while(|&c| c != -1)
            .map(TryInto::try_into)
            .collect::<Result<Vec<_>, _>>()?;
        Ok(Self {
            stream_id: info.stream_id,
            dev_idx: info.dev_idx,
            direction: info.direction.into(),
            stream_type: info.stream_type.try_into()?,
            client_type: info.client_type.try_into()?,
            buffer_frames: info.buffer_frames,
            cb_threshold: info.cb_threshold,
            effects: info.effects,
            flags: info.flags,
            frame_rate: info.frame_rate,
            num_channels: info.num_channels,
            longest_fetch: Duration::new(info.longest_fetch_sec.into(), info.longest_fetch_nsec),
            num_missed_cb: info.num_missed_cb,
            num_overruns: info.num_overruns,
            is_pinned: info.is_pinned != 0,
            pinned_dev_idx: info.pinned_dev_idx,
            runtime: Duration::new(info.runtime_sec.into(), info.runtime_nsec),
            stream_volume: info.stream_volume,
            channel_layout,
        })
    }
}

impl fmt::Display for AudioStreamDebugInfo {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        writeln!(
            f,
            "Stream: {}, Device index: {}",
            self.stream_id, self.dev_idx
        )?;
        writeln!(f, "  Direction: {:?}", self.direction)?;
        writeln!(f, "  Stream type: {:?}", self.stream_type)?;
        writeln!(f, "  Client type: {:?}", self.client_type)?;
        writeln!(f, "  Buffer frames: {}", self.buffer_frames)?;
        writeln!(f, "  Callback threshold: {}", self.cb_threshold)?;
        writeln!(f, "  Effects: {:#x}", self.effects)?;
        writeln!(f, "  Frame rate: {}", self.frame_rate)?;
        writeln!(f, "  Number of channels: {}", self.num_channels)?;
        writeln!(f, "  Longest fetch: {:?}", self.longest_fetch)?;
        writeln!(f, "  Overrun count: {}", self.num_overruns)?;
        writeln!(f, "  Pinned: {}", self.is_pinned)?;
        writeln!(f, "  Pinned device index: {}", self.pinned_dev_idx)?;
        writeln!(f, "  Missed callbacks: {}", self.num_missed_cb)?;
        match self.direction {
            CRAS_STREAM_DIRECTION::CRAS_STREAM_OUTPUT => {
                writeln!(f, "  Volume: {:.2}", self.stream_volume)?
            }
            CRAS_STREAM_DIRECTION::CRAS_STREAM_INPUT => {
                writeln!(f, "  Gain: {:.2}", self.stream_volume)?
            }
            _ => (),
        };
        writeln!(f, "  Runtime: {:?}", self.runtime)?;
        write!(f, "  Channel map:")?;
        for channel in &self.channel_layout {
            write!(f, " {:?}", channel)?;
        }
        writeln!(f)?;
        Ok(())
    }
}

/// A rust-style representation of the server's audio debug info.
pub struct AudioDebugInfo {
    pub devices: Vec<AudioDevDebugInfo>,
    pub streams: Vec<AudioStreamDebugInfo>,
}

impl AudioDebugInfo {
    pub fn new(devices: Vec<AudioDevDebugInfo>, streams: Vec<AudioStreamDebugInfo>) -> Self {
        Self { devices, streams }
    }
}

impl Into<u64> for CRAS_STREAM_EFFECT {
    fn into(self) -> u64 {
        u64::from(self.0)
    }
}

impl CRAS_STREAM_EFFECT {
    pub fn empty() -> Self {
        CRAS_STREAM_EFFECT(0)
    }
}
