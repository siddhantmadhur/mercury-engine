// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! USS stylesheet tokenizer backed by the `cssparser` crate.
//!
//! Plumbing lands first (WP0): the bridge starts empty and the tokenizer
//! API fills in as the USS parser migrates off its hand-written tokenizer.

#[cxx::bridge(namespace = "Mercury::uss")]
mod ffi {}
