// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

use std::fmt;

/// Error type for Mercury Rust subsystem operations.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Error {
    NotFound(String),
    InvalidUtf8,
    InvalidArgument(&'static str),
    Io(String),
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::NotFound(path) => write!(f, "not found: {path}"),
            Error::InvalidUtf8 => write!(f, "invalid UTF-8"),
            Error::InvalidArgument(msg) => write!(f, "invalid argument: {msg}"),
            Error::Io(msg) => write!(f, "I/O error: {msg}"),
        }
    }
}

impl std::error::Error for Error {}

/// Result type alias used across Mercury Rust modules (analogous to `kernel::error::Result`).
pub type Result<T = ()> = std::result::Result<T, Error>;
