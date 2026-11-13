use crate::ffi::bridge;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Level {
    Debug,
    Info,
    Warn,
    Error,
}

#[doc(hidden)]
pub fn _emit(level: Level, args: std::fmt::Arguments<'_>) {
    let msg = args.to_string();
    match level {
        Level::Debug => bridge::log_debug(&msg),
        Level::Info => bridge::log_info(&msg),
        Level::Warn => bridge::log_warn(&msg),
        Level::Error => bridge::log_error(&msg),
    }
}

#[macro_export]
macro_rules! debug {
    ($($arg:tt)*) => {
        $crate::log::_emit($crate::log::Level::Debug, format_args!($($arg)*))
    };
}

#[macro_export]
macro_rules! info {
    ($($arg:tt)*) => {
        $crate::log::_emit($crate::log::Level::Info, format_args!($($arg)*))
    };
}

#[macro_export]
macro_rules! warn {
    ($($arg:tt)*) => {
        $crate::log::_emit($crate::log::Level::Warn, format_args!($($arg)*))
    };
}

#[macro_export]
macro_rules! error {
    ($($arg:tt)*) => {
        $crate::log::_emit($crate::log::Level::Error, format_args!($($arg)*))
    };
}
