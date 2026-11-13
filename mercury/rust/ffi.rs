use std::sync::OnceLock;

#[cxx::bridge(namespace = "Mercury::rust")]
pub mod bridge {
    unsafe extern "C++" {
        include!("rust/bindings.h");

        #[cxx_name = "LogDebug"]
        fn log_debug(message: &str);
        #[cxx_name = "LogInfo"]
        fn log_info(message: &str);
        #[cxx_name = "LogWarn"]
        fn log_warn(message: &str);
        #[cxx_name = "LogError"]
        fn log_error(message: &str);

        #[cxx_name = "OsExists"]
        fn os_exists(path: &str) -> bool;
        #[cxx_name = "OsIsPacked"]
        fn os_is_packed(path: &str) -> bool;
        #[cxx_name = "OsHasMounts"]
        fn os_has_mounts() -> bool;
        #[cxx_name = "OsReadBytes"]
        fn os_read_bytes(path: &str, out: &mut Vec<u8>) -> bool;
        #[cxx_name = "OsCacheDir"]
        fn os_cache_dir() -> String;
        #[cxx_name = "OsExecutableDir"]
        fn os_executable_dir() -> String;

        #[cxx_name = "EngineVersion"]
        fn engine_version() -> &'static str;
    }

    extern "Rust" {
        #[cxx_name = "Init"]
        fn rust_init() -> bool;
        #[cxx_name = "IsEnabled"]
        fn rust_is_enabled() -> bool;
        #[cxx_name = "Version"]
        fn rust_version() -> &'static str;
        #[cxx_name = "RoundTripSelfTest"]
        fn rust_round_trip_self_test(input: &str) -> String;
        #[cxx_name = "LogInfoFromRust"]
        fn rust_log_info(message: &str);
        #[cxx_name = "ReadTextFromRust"]
        fn rust_read_text(path: &str) -> Result<String>;
    }
}

static INITIALIZED: OnceLock<bool> = OnceLock::new();

fn rust_init() -> bool {
    *INITIALIZED.get_or_init(|| true)
}

fn rust_is_enabled() -> bool {
    true
}

fn rust_version() -> &'static str {
    crate::engine_version()
}

fn rust_round_trip_self_test(input: &str) -> String {
    let engine_ver = crate::engine_version();
    format!("rust:{input}:engine={engine_ver}")
}

fn rust_log_info(message: &str) {
    crate::info!("{message}");
}

fn rust_read_text(path: &str) -> crate::error::Result<String> {
    crate::fs::read_text(path)
}
