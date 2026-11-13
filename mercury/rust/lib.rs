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
