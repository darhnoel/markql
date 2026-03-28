use std::fs;
use std::path::PathBuf;

use rand::{distributions::Alphanumeric, Rng};
use serde::{Deserialize, Serialize};
use thiserror::Error;

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct DesktopSettings {
    pub token: String,
    pub start_on_login: bool,
    pub query_timeout_ms: u64,
    pub port: u16,
}

impl Default for DesktopSettings {
    fn default() -> Self {
        Self {
            token: generate_token(),
            start_on_login: false,
            query_timeout_ms: 5_000,
            port: 7_337,
        }
    }
}

#[derive(Debug, Error)]
pub enum ConfigError {
    #[error("failed to locate a writable app config directory")]
    MissingConfigDir,
    #[error("failed to create config directory: {0}")]
    CreateDir(#[source] std::io::Error),
    #[error("failed to read settings: {0}")]
    Read(#[source] std::io::Error),
    #[error("failed to parse settings: {0}")]
    Parse(#[source] serde_json::Error),
    #[error("failed to serialize settings: {0}")]
    Serialize(#[source] serde_json::Error),
    #[error("failed to write settings: {0}")]
    Write(#[source] std::io::Error),
    #[cfg(target_os = "windows")]
    #[error("failed to update start-on-login integration: {0}")]
    Autostart(String),
}

#[derive(Debug, Clone)]
pub struct ConfigStore {
    settings_path: PathBuf,
}

impl ConfigStore {
    pub fn new() -> Result<Self, ConfigError> {
        let base_dir = dirs::config_dir().ok_or(ConfigError::MissingConfigDir)?;
        let config_dir = base_dir.join("markql-desktop");
        fs::create_dir_all(&config_dir).map_err(ConfigError::CreateDir)?;
        Ok(Self {
            settings_path: config_dir.join("settings.json"),
        })
    }

    pub fn load(&self) -> Result<DesktopSettings, ConfigError> {
        if !self.settings_path.exists() {
            let settings = DesktopSettings::default();
            self.save(&settings)?;
            return Ok(settings);
        }
        let contents = fs::read_to_string(&self.settings_path).map_err(ConfigError::Read)?;
        serde_json::from_str(&contents).map_err(ConfigError::Parse)
    }

    pub fn save(&self, settings: &DesktopSettings) -> Result<(), ConfigError> {
        let json = serde_json::to_string_pretty(settings).map_err(ConfigError::Serialize)?;
        fs::write(&self.settings_path, json).map_err(ConfigError::Write)?;
        Ok(())
    }

    pub fn persist(&self, settings: &DesktopSettings) -> Result<(), ConfigError> {
        self.save(settings)?;
        self.apply_start_on_login(settings)?;
        Ok(())
    }

    fn apply_start_on_login(&self, settings: &DesktopSettings) -> Result<(), ConfigError> {
        #[cfg(target_os = "linux")]
        {
            let autostart_dir = dirs::config_dir()
                .ok_or(ConfigError::MissingConfigDir)?
                .join("autostart");
            fs::create_dir_all(&autostart_dir).map_err(ConfigError::CreateDir)?;
            let desktop_path = autostart_dir.join("markql-desktop.desktop");
            if settings.start_on_login {
                let entry = format!(
                    "[Desktop Entry]\nType=Application\nName=MarkQL Desktop\nExec={}\nX-GNOME-Autostart-enabled=true\n",
                    current_exe_for_autostart().display()
                );
                fs::write(desktop_path, entry).map_err(ConfigError::Write)?;
            } else if desktop_path.exists() {
                fs::remove_file(desktop_path).map_err(ConfigError::Write)?;
            }
            return Ok(());
        }

        #[cfg(target_os = "windows")]
        {
            let exe = current_exe_for_autostart();
            let quoted = format!("\"{}\"", exe.display());
            let status = if settings.start_on_login {
                std::process::Command::new("reg")
                    .args([
                        "add",
                        r"HKCU\Software\Microsoft\Windows\CurrentVersion\Run",
                        "/v",
                        "MarkQLDesktop",
                        "/t",
                        "REG_SZ",
                        "/d",
                        &quoted,
                        "/f",
                    ])
                    .status()
            } else {
                std::process::Command::new("reg")
                    .args([
                        "delete",
                        r"HKCU\Software\Microsoft\Windows\CurrentVersion\Run",
                        "/v",
                        "MarkQLDesktop",
                        "/f",
                    ])
                    .status()
            }
            .map_err(|err| ConfigError::Autostart(err.to_string()))?;
            if !status.success() {
                return Err(ConfigError::Autostart(
                    "registry update failed while applying start-on-login".to_string(),
                ));
            }
            return Ok(());
        }

        #[allow(unreachable_code)]
        Ok(())
    }
}

pub fn clamp_timeout_ms(value: u64) -> u64 {
    match value {
        0..=4_999 => 5_000,
        5_000 => 5_000,
        5_001..=10_000 => 10_000,
        10_001..=30_000 => 30_000,
        _ => 60_000,
    }
}

pub fn normalize_port(port: u16) -> u16 {
    if port == 0 {
        7_337
    } else {
        port
    }
}

pub fn generate_token() -> String {
    rand::thread_rng()
        .sample_iter(&Alphanumeric)
        .take(48)
        .map(char::from)
        .collect()
}

fn current_exe_for_autostart() -> PathBuf {
    std::env::current_exe().unwrap_or_else(|_| PathBuf::from("markql-desktop"))
}
