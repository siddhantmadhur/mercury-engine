// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.

//! Common imports for Mercury Rust feature modules (analogous to `kernel::prelude::*`).

pub use crate::error::{Error, Result};
pub use crate::{debug, error, info, warn};
pub use cxx;
