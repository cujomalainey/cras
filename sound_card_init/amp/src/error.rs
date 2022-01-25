// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![allow(missing_docs)]

use remain::sorted;
use thiserror::Error as ThisError;

use crate::max98373d;
use crate::max98390d;

pub type Result<T> = std::result::Result<T, Error>;

#[sorted]
#[derive(Debug, ThisError)]
pub enum Error {
    #[error(transparent)]
    AlsaCardError(#[from] cros_alsa::CardError),
    #[error(transparent)]
    AlsaControlError(#[from] cros_alsa::ControlError),
    #[error(transparent)]
    AlsaControlTLVError(#[from] cros_alsa::ControlTLVError),
    #[error(transparent)]
    DSMError(#[from] dsm::Error),
    #[error(transparent)]
    Max98373Error(#[from] max98373d::Error),
    #[error(transparent)]
    Max98390Error(#[from] max98390d::Error),
    #[error("unsupported amp: {0}")]
    UnsupportedAmp(String),
}