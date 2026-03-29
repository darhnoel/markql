mod agent;
mod config;
mod logging;

use std::path::PathBuf;

use agent::{AgentManager, AgentStatus};
use config::{ConfigStore, DesktopSettings};
use tauri::menu::{MenuBuilder, MenuItemBuilder, PredefinedMenuItem};
use tauri::tray::TrayIconBuilder;
use tauri::{AppHandle, Emitter, Manager, State};

struct DesktopState {
    manager: AgentManager,
}

#[tauri::command]
async fn get_settings(state: State<'_, DesktopState>) -> Result<DesktopSettings, String> {
    Ok(state.manager.settings().await)
}

#[tauri::command]
async fn save_settings(
    state: State<'_, DesktopState>,
    app: AppHandle,
    settings: DesktopSettings,
) -> Result<DesktopSettings, String> {
    let saved = state
        .manager
        .update_settings(settings)
        .await
        .map_err(|err| err.to_string())?;
    let status = state.manager.status().await;
    let _ = app.emit("agent-status", status);
    Ok(saved)
}

#[tauri::command]
async fn regenerate_token(state: State<'_, DesktopState>) -> Result<DesktopSettings, String> {
    state
        .manager
        .regenerate_token()
        .await
        .map_err(|err| err.to_string())
}

#[tauri::command]
async fn copy_token(state: State<'_, DesktopState>) -> Result<(), String> {
    state
        .manager
        .copy_token()
        .await
        .map_err(|err| err.to_string())
}

#[tauri::command]
async fn start_agent(
    state: State<'_, DesktopState>,
    app: AppHandle,
) -> Result<AgentStatus, String> {
    state
        .manager
        .start(Some(app))
        .await
        .map_err(|err| err.to_string())
}

#[tauri::command]
async fn stop_agent(state: State<'_, DesktopState>, app: AppHandle) -> Result<AgentStatus, String> {
    state
        .manager
        .stop(Some(app))
        .await
        .map_err(|err| err.to_string())
}

#[tauri::command]
async fn get_status(state: State<'_, DesktopState>) -> Result<AgentStatus, String> {
    Ok(state.manager.status().await)
}

#[tauri::command]
fn show_settings(app: AppHandle) -> Result<(), String> {
    show_settings_window(&app)
}

fn show_settings_window(app: &AppHandle) -> Result<(), String> {
    logging::log("show_settings_window requested");
    let window = app
        .get_webview_window("settings")
        .ok_or_else(|| "settings window is not available".to_string())?;
    window.show().map_err(|err| err.to_string())?;
    window.set_focus().map_err(|err| err.to_string())?;
    Ok(())
}

fn build_tray(app: &AppHandle) -> Result<(), Box<dyn std::error::Error>> {
    logging::log("building tray menu");
    let status = MenuItemBuilder::with_id("status", "MarkQL Agent: Stopped")
        .enabled(false)
        .build(app)?;
    let start = MenuItemBuilder::with_id("start", "Start Agent").build(app)?;
    let stop = MenuItemBuilder::with_id("stop", "Stop Agent").build(app)?;
    let copy = MenuItemBuilder::with_id("copy-token", "Copy Token").build(app)?;
    let settings = MenuItemBuilder::with_id("settings", "Settings…").build(app)?;
    let quit = MenuItemBuilder::with_id("quit", "Quit MarkQL").build(app)?;
    let separator = PredefinedMenuItem::separator(app)?;

    let menu = MenuBuilder::new(app)
        .item(&status)
        .item(&separator)
        .item(&start)
        .item(&stop)
        .item(&copy)
        .item(&settings)
        .item(&quit)
        .build()?;

    let state = app.state::<DesktopState>().manager.clone();
    let app_handle = app.clone();
    TrayIconBuilder::with_id("markql-tray")
        .menu(&menu)
        .show_menu_on_left_click(true)
        .on_menu_event(move |app, event| {
            let manager = state.clone();
            let app_handle = app_handle.clone();
            match event.id().as_ref() {
                "start" => {
                    tauri::async_runtime::spawn(async move {
                        if let Err(err) = manager.start(Some(app_handle.clone())).await {
                            logging::log(format!("tray start failed: {err}"));
                            eprintln!("failed to start markql-agent from tray: {err}");
                            let status = manager.status().await;
                            let _ = app_handle.emit("agent-status", status);
                            let _ = show_settings_window(&app_handle);
                        }
                    });
                }
                "stop" => {
                    tauri::async_runtime::spawn(async move {
                        if let Err(err) = manager.stop(Some(app_handle.clone())).await {
                            logging::log(format!("tray stop failed: {err}"));
                            eprintln!("failed to stop markql-agent from tray: {err}");
                            let status = manager.status().await;
                            let _ = app_handle.emit("agent-status", status);
                            let _ = show_settings_window(&app_handle);
                        }
                    });
                }
                "copy-token" => {
                    tauri::async_runtime::spawn(async move {
                        if let Err(err) = manager.copy_token().await {
                            logging::log(format!("tray copy token failed: {err}"));
                            eprintln!("failed to copy MarkQL agent token from tray: {err}");
                            let status = manager.status().await;
                            let _ = app_handle.emit("agent-status", status);
                            let _ = show_settings_window(&app_handle);
                        }
                    });
                }
                "settings" => {
                    logging::log("tray settings clicked");
                    let _ = show_settings_window(app);
                }
                "quit" => {
                    logging::log("tray quit clicked");
                    let app_handle = app.clone();
                    tauri::async_runtime::spawn(async move {
                        let _ = manager.stop(Some(app_handle.clone())).await;
                        app_handle.exit(0);
                    });
                }
                _ => {}
            }
        })
        .build(app)?;

    Ok(())
}

fn repo_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(env!("CARGO_MANIFEST_DIR")))
}

#[cfg(target_os = "linux")]
fn sanitize_linux_gui_env() {
    let running_from_appimage =
        std::env::var_os("APPIMAGE").is_some() || std::env::var_os("APPDIR").is_some();
    let snap_tainted = std::env::var_os("SNAP").is_some()
        || [
            "GTK_PATH",
            "GTK_EXE_PREFIX",
            "GTK_IM_MODULE_FILE",
            "GIO_MODULE_DIR",
            "GIO_EXTRA_MODULES",
            "XDG_DATA_DIRS",
        ]
        .into_iter()
        .filter_map(std::env::var_os)
        .any(|value| value.to_string_lossy().contains("/snap/"));

    for key in ["GTK_MODULES", "GTK3_MODULES"] {
        if let Some(value) = std::env::var_os(key) {
            let filtered = value
                .to_string_lossy()
                .split(':')
                .filter(|module| *module != "xapp-gtk3-module")
                .collect::<Vec<_>>()
                .join(":");
            if filtered.is_empty() {
                std::env::remove_var(key);
            } else {
                std::env::set_var(key, filtered);
            }
        }
    }

    if snap_tainted {
        for key in [
            "SNAP",
            "GTK_PATH",
            "GTK_EXE_PREFIX",
            "GTK_IM_MODULE_FILE",
            "XDG_DATA_DIRS",
        ] {
            std::env::remove_var(key);
        }
    }

    if snap_tainted || running_from_appimage {
        // Prevent host GIO/GVFS modules from loading against bundled GLib/AppImage libs.
        std::env::set_var("GIO_MODULE_DIR", "/nonexistent");
        std::env::set_var("GIO_EXTRA_MODULES", "/nonexistent");
        std::env::set_var("GIO_USE_VFS", "local");
    }
}

fn main() {
    let log_path = logging::init();
    logging::install_panic_hook();
    if let Some(path) = log_path {
        logging::log(format!("startup log path: {}", path.display()));
    }

    #[cfg(target_os = "linux")]
    {
        logging::log("sanitizing linux GUI environment");
        sanitize_linux_gui_env();
    }

    logging::log("creating Tauri builder");
    let builder = tauri::Builder::default()
        .on_page_load(|window, payload| {
            logging::log(format!(
                "frontend loaded for window '{}' at {}",
                window.label(),
                payload.url().as_str()
            ));
        })
        .setup(|app| {
            logging::log("tauri setup begin");
            let config = ConfigStore::new().map_err(|err| {
                logging::log(format!("config store init failed: {err}"));
                err.to_string()
            })?;
            logging::log("config store initialized");
            let settings = config.load().map_err(|err| {
                logging::log(format!("config load failed: {err}"));
                err.to_string()
            })?;
            logging::log(format!(
                "settings loaded: port={}, timeout_ms={}, start_on_login={}",
                settings.port, settings.query_timeout_ms, settings.start_on_login
            ));
            app.manage(DesktopState {
                manager: AgentManager::new(config, settings, repo_root()),
            });
            build_tray(app.handle()).map_err(|err| {
                logging::log(format!("tray build failed: {err}"));
                err.to_string()
            })?;
            logging::log("tray build complete");
            if let Some(window) = app.get_webview_window("settings") {
                logging::log("settings window found");
                if cfg!(debug_assertions) {
                    logging::log("debug build: showing settings window on startup");
                    let _ = window.show();
                    let _ = window.set_focus();
                } else {
                    logging::log("release build: hiding settings window on startup");
                    let _ = window.hide();
                }
            } else {
                logging::log("settings window not found during setup");
            }
            logging::log("tauri setup complete");
            Ok(())
        });

    logging::log("registering commands and running app");
    let result = builder
        .invoke_handler(tauri::generate_handler![
            get_settings,
            save_settings,
            regenerate_token,
            copy_token,
            start_agent,
            stop_agent,
            get_status,
            show_settings
        ])
        .run(tauri::generate_context!());

    match result {
        Ok(()) => logging::log("app exited normally"),
        Err(err) => {
            logging::log(format!("error while running MarkQL Desktop: {err}"));
            panic!("error while running MarkQL Desktop: {err}");
        }
    }
}
