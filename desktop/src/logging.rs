use std::fs::{self, File, OpenOptions};
use std::io::Write;
use std::path::PathBuf;
use std::sync::{Mutex, OnceLock};
use std::time::{SystemTime, UNIX_EPOCH};

struct DesktopLogger {
    path: PathBuf,
    file: Mutex<File>,
}

static LOGGER: OnceLock<Option<DesktopLogger>> = OnceLock::new();

fn now_epoch_seconds() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|duration| duration.as_secs())
        .unwrap_or(0)
}

fn build_log_path() -> Option<PathBuf> {
    let base_dir = dirs::data_local_dir().or_else(dirs::config_dir)?;
    Some(base_dir.join("markql-desktop").join("logs").join("desktop.log"))
}

pub fn init() -> Option<PathBuf> {
    let logger = LOGGER.get_or_init(|| {
        let path = build_log_path()?;
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent).ok()?;
        }
        let file = OpenOptions::new()
            .create(true)
            .append(true)
            .open(&path)
            .ok()?;
        Some(DesktopLogger {
            path,
            file: Mutex::new(file),
        })
    });

    let path = logger.as_ref().map(|logger| logger.path.clone());
    log(format!(
        "=== startup pid={} version={} epoch={} ===",
        std::process::id(),
        env!("CARGO_PKG_VERSION"),
        now_epoch_seconds()
    ));
    path
}

pub fn log(message: impl AsRef<str>) {
    let Some(Some(logger)) = LOGGER.get() else {
        return;
    };
    if let Ok(mut file) = logger.file.lock() {
        let _ = writeln!(file, "[{}] {}", now_epoch_seconds(), message.as_ref());
        let _ = file.flush();
    }
}

pub fn install_panic_hook() {
    std::panic::set_hook(Box::new(|panic_info| {
        log(format!("panic: {panic_info}"));
    }));
}
