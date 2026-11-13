//! Rust feature crates linked into the Mercury Engine static library.
//!
//! Each feature crate (such as `os_filesystem_rs`) is compiled with
//! `mercury_kernel` and bundled here so all Rust code, `cxx`, `std`, and the
//! panic runtime are linked once into `//mercury:mercury`.

#[allow(unused_imports)]
use mercury_kernel as _;

#[allow(unused_imports)]
use os_filesystem_rs as _;
