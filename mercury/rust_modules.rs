// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Rust feature crates linked into the Mercury Engine static library.
//!
//! Each feature crate (such as `os_filesystem_rs`) is compiled with
//! `mercury_kernel` and bundled here so all Rust code, `cxx`, `std`, and the
//! panic runtime are linked once into `//mercury:mercury`.

#[allow(unused_imports)]
use mercury_kernel as _;

#[allow(unused_imports)]
use input_actions_codegen_rs as _;

#[allow(unused_imports)]
use mercury_uss as _;
