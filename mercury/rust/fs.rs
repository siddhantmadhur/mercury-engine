use crate::error::{Error, Result};
use crate::ffi::bridge;

/// Returns true if `path` exists either in a mounted `.mpack` or on disk.
pub fn exists(path: &str) -> bool {
    bridge::os_exists(path)
}

/// Returns true if `path` is served from a mounted `.mpack`.
pub fn is_packed(path: &str) -> bool {
    bridge::os_is_packed(path)
}

/// Returns true if any `.mpack` archive is currently mounted.
pub fn has_mounts() -> bool {
    bridge::os_has_mounts()
}

/// Reads all bytes from `path` via `Mercury::os::ReadBytes` (mounted packs or disk).
pub fn read_bytes(path: &str) -> Result<Vec<u8>> {
    let mut out = Vec::new();
    if bridge::os_read_bytes(path, &mut out) {
        Ok(out)
    } else {
        Err(Error::NotFound(path.to_owned()))
    }
}

/// Reads UTF-8 text from `path` via `Mercury::os::ReadBytes` (mounted packs or disk).
pub fn read_text(path: &str) -> Result<String> {
    let bytes = read_bytes(path)?;
    String::from_utf8(bytes).map_err(|_| Error::InvalidUtf8)
}

/// Returns the engine cache directory (`Mercury::os::CacheDir`).
pub fn cache_dir() -> String {
    bridge::os_cache_dir()
}

/// Returns the directory containing the running executable (`Mercury::os::ExecutableDir`).
pub fn executable_dir() -> String {
    bridge::os_executable_dir()
}
