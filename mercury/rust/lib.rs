// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Mercury Engine Rust support crate.
//!
//! Analogous to the Linux kernel's `kernel` crate (`rust/kernel/lib.rs`), this
//! crate bridges Mercury's C++ engine core into Rust via `cxx` and provides
//! safe, idiomatic wrappers (`prelude`, `log`, `fs`, `error`) for Rust-implemented
//! engine features.

pub mod error;
pub mod ffi;
pub mod fs;
pub mod log;
pub mod prelude;

/// Returns the engine version string reported by the C++ core.
pub fn engine_version() -> &'static str {
    ffi::bridge::engine_version()
}
