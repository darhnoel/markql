use std::net::TcpStream;
use std::path::{Path, PathBuf};
use std::process::Stdio;
use std::sync::Arc;

use arboard::Clipboard;
use serde::Serialize;
use tauri::{AppHandle, Emitter, Manager};
use thiserror::Error;
use tokio::process::{Child, Command};
use tokio::sync::Mutex;
use tokio::time::{sleep, timeout, Duration, Instant};

use crate::config::{
    clamp_timeout_ms, generate_token, normalize_port, ConfigStore, DesktopSettings,
};
use crate::logging;

const STATUS_EVENT: &str = "agent-status";
const MAX_RESTARTS: u8 = 3;

#[derive(Debug, Clone, Serialize)]
pub struct AgentStatus {
    pub running: bool,
    pub label: String,
    pub managed_process: bool,
    pub port: u16,
    pub restart_attempts: u8,
    pub last_error: Option<String>,
}

#[derive(Debug, Error)]
pub enum AgentError {
    #[error("failed to resolve markql-agent binary")]
    BinaryNotFound,
    #[error("failed to start markql-agent: {0}")]
    Spawn(#[source] std::io::Error),
    #[error("markql-agent did not become healthy within 5 seconds")]
    HealthTimeout,
    #[error("port {0} is already in use by a non-MarkQL process")]
    PortBusy(u16),
    #[error("failed to stop markql-agent: {0}")]
    Stop(String),
    #[error("failed to copy token: {0}")]
    Clipboard(String),
    #[error("failed to persist settings: {0}")]
    Persist(String),
}

#[derive(Debug)]
struct ManagedChild {
    child: Child,
    pid: Option<u32>,
}

#[derive(Debug)]
struct InnerState {
    settings: DesktopSettings,
    child: Option<ManagedChild>,
    status_label: String,
    managed_process: bool,
    restart_attempts: u8,
    last_error: Option<String>,
    generation: u64,
}

#[derive(Clone)]
pub struct AgentManager {
    config: ConfigStore,
    repo_root: PathBuf,
    inner: Arc<Mutex<InnerState>>,
}

impl AgentManager {
    pub fn new(config: ConfigStore, settings: DesktopSettings, repo_root: PathBuf) -> Self {
        Self {
            config,
            repo_root,
            inner: Arc::new(Mutex::new(InnerState {
                settings,
                child: None,
                status_label: "Stopped".to_string(),
                managed_process: false,
                restart_attempts: 0,
                last_error: None,
                generation: 0,
            })),
        }
    }

    pub async fn settings(&self) -> DesktopSettings {
        self.inner.lock().await.settings.clone()
    }

    pub async fn update_settings(
        &self,
        mut settings: DesktopSettings,
    ) -> Result<DesktopSettings, AgentError> {
        settings.port = normalize_port(settings.port);
        settings.query_timeout_ms = clamp_timeout_ms(settings.query_timeout_ms);
        if settings.token.trim().is_empty() {
            settings.token = generate_token();
        }
        self.config
            .persist(&settings)
            .map_err(|err| AgentError::Persist(err.to_string()))?;
        let mut inner = self.inner.lock().await;
        inner.settings = settings.clone();
        Ok(settings)
    }

    pub async fn regenerate_token(&self) -> Result<DesktopSettings, AgentError> {
        let mut settings = self.settings().await;
        settings.token = generate_token();
        self.update_settings(settings).await
    }

    pub async fn copy_token(&self) -> Result<(), AgentError> {
        let token = self.settings().await.token;
        let mut clipboard =
            Clipboard::new().map_err(|err| AgentError::Clipboard(err.to_string()))?;
        clipboard
            .set_text(token)
            .map_err(|err| AgentError::Clipboard(err.to_string()))
    }

    pub async fn status(&self) -> AgentStatus {
        let inner = self.inner.lock().await;
        AgentStatus {
            running: inner.status_label.starts_with("Running"),
            label: inner.status_label.clone(),
            managed_process: inner.managed_process,
            port: inner.settings.port,
            restart_attempts: inner.restart_attempts,
            last_error: inner.last_error.clone(),
        }
    }

    pub async fn start(&self, app: Option<AppHandle>) -> Result<AgentStatus, AgentError> {
        let settings = self.settings().await;
        logging::log(format!(
            "agent start requested: port={}, timeout_ms={}",
            settings.port, settings.query_timeout_ms
        ));
        if self.is_healthy(settings.port).await {
            logging::log(format!("external agent already healthy on port {}", settings.port));
            let status = {
                let mut inner = self.inner.lock().await;
                inner.child = None;
                inner.managed_process = false;
                inner.status_label = "Running (external agent)".to_string();
                inner.last_error = None;
                self.snapshot_locked(&inner)
            };
            self.emit_status(app.as_ref(), &status);
            return Ok(status);
        }

        if port_is_open(settings.port) {
            logging::log(format!("port {} is already busy", settings.port));
            return Err(AgentError::PortBusy(settings.port));
        }

        let binary = self.resolve_binary(&app)?;
        logging::log(format!("resolved markql-agent binary: {}", binary.display()));
        let generation = {
            let mut inner = self.inner.lock().await;
            if inner.child.is_some() {
                inner.status_label = "Running".to_string();
                return Ok(self.snapshot_locked(&inner));
            }
            let mut command = Command::new(binary);
            command
                .env("MARKQL_AGENT_TOKEN", &settings.token)
                .env("XSQL_AGENT_TOKEN", &settings.token)
                .env("MARKQL_AGENT_PORT", settings.port.to_string())
                .stdout(Stdio::null())
                .stderr(Stdio::null());
            let child = command.spawn().map_err(AgentError::Spawn)?;
            logging::log(format!(
                "spawned managed markql-agent pid={:?}",
                child.id()
            ));
            inner.generation += 1;
            inner.child = Some(ManagedChild {
                pid: child.id(),
                child,
            });
            inner.managed_process = true;
            inner.restart_attempts = 0;
            inner.last_error = None;
            inner.status_label = "Starting".to_string();
            inner.generation
        };

        if let Err(err) = self.wait_for_health(settings.port).await {
            logging::log(format!("markql-agent health check failed: {err}"));
            let child = {
                let mut inner = self.inner.lock().await;
                inner.generation += 1;
                inner.status_label = "Stopped".to_string();
                inner.managed_process = false;
                inner.last_error = Some(err.to_string());
                inner.child.take()
            };
            if let Some(managed_child) = child {
                let _ = stop_child(managed_child).await;
            }
            let status = self.status().await;
            self.emit_status(app.as_ref(), &status);
            return Err(err);
        }

        {
            let mut inner = self.inner.lock().await;
            inner.status_label = "Running".to_string();
        }
        logging::log(format!("managed markql-agent healthy on port {}", settings.port));
        let status = self.status().await;
        self.emit_status(app.as_ref(), &status);

        if let Some(app_handle) = app {
            let manager = self.clone();
            tauri::async_runtime::spawn(async move {
                manager.supervise(app_handle, generation).await;
            });
        }

        Ok(status)
    }

    pub async fn stop(&self, app: Option<AppHandle>) -> Result<AgentStatus, AgentError> {
        logging::log("agent stop requested");
        let child = {
            let mut inner = self.inner.lock().await;
            inner.generation += 1;
            inner.status_label = "Stopped".to_string();
            inner.last_error = None;
            inner.managed_process = false;
            inner.restart_attempts = 0;
            inner.child.take()
        };

        if let Some(managed_child) = child {
            stop_child(managed_child)
                .await
                .map_err(|err| AgentError::Stop(err.to_string()))?;
        }

        let status = self.status().await;
        self.emit_status(app.as_ref(), &status);
        Ok(status)
    }

    fn snapshot_locked(&self, inner: &InnerState) -> AgentStatus {
        AgentStatus {
            running: inner.status_label.starts_with("Running"),
            label: inner.status_label.clone(),
            managed_process: inner.managed_process,
            port: inner.settings.port,
            restart_attempts: inner.restart_attempts,
            last_error: inner.last_error.clone(),
        }
    }

    fn emit_status(&self, app: Option<&AppHandle>, status: &AgentStatus) {
        logging::log(format!(
            "status update: running={}, label={}, managed={}, port={}, restarts={}, last_error={}",
            status.running,
            status.label,
            status.managed_process,
            status.port,
            status.restart_attempts,
            status.last_error.as_deref().unwrap_or("none")
        ));
        if let Some(app_handle) = app {
            let _ = app_handle.emit(STATUS_EVENT, status);
            if let Some(window) = app_handle.get_webview_window("settings") {
                let _ = window.emit(STATUS_EVENT, status);
            }
        }
    }

    async fn supervise(&self, app: AppHandle, mut generation: u64) {
        loop {
            sleep(Duration::from_millis(500)).await;
            let outcome = {
                let mut inner = self.inner.lock().await;
                if inner.generation != generation {
                    return;
                }
                let Some(child) = inner.child.as_mut() else {
                    return;
                };
                match child.child.try_wait() {
                    Ok(Some(status)) => Some((
                        status.success(),
                        status.code().unwrap_or(-1),
                        inner.restart_attempts,
                    )),
                    Ok(None) => None,
                    Err(err) => {
                        inner.last_error = Some(err.to_string());
                        inner.status_label = "Stopped".to_string();
                        inner.child = None;
                        Some((false, -1, inner.restart_attempts))
                    }
                }
            };

            let Some((success, exit_code, attempts)) = outcome else {
                continue;
            };
            if success {
                let status = self.status().await;
                self.emit_status(Some(&app), &status);
                return;
            }

            if attempts >= MAX_RESTARTS {
                logging::log(format!(
                    "markql-agent restart limit reached after exit code {}",
                    exit_code
                ));
                let status = {
                    let mut inner = self.inner.lock().await;
                    inner.child = None;
                    inner.managed_process = false;
                    inner.status_label = "Stopped (restart limit reached)".to_string();
                    inner.last_error = Some(format!("markql-agent exited with code {}", exit_code));
                    self.snapshot_locked(&inner)
                };
                self.emit_status(Some(&app), &status);
                return;
            }

            let settings = {
                let mut inner = self.inner.lock().await;
                inner.child = None;
                inner.restart_attempts += 1;
                inner.status_label =
                    format!("Restarting ({}/{})", inner.restart_attempts, MAX_RESTARTS);
                inner.last_error = Some(format!("markql-agent exited with code {}", exit_code));
                inner.settings.clone()
            };
            self.emit_status(Some(&app), &self.status().await);

            if let Ok(binary) = self.resolve_binary(&Some(app.clone())) {
                logging::log(format!(
                    "attempting markql-agent restart from {}",
                    binary.display()
                ));
                let spawn_result = {
                    let mut command = Command::new(binary);
                    command
                        .env("MARKQL_AGENT_TOKEN", &settings.token)
                        .env("XSQL_AGENT_TOKEN", &settings.token)
                        .env("MARKQL_AGENT_PORT", settings.port.to_string())
                        .stdout(Stdio::null())
                        .stderr(Stdio::null());
                    command.spawn()
                };

                match spawn_result {
                    Ok(child) => {
                        logging::log(format!("respawned markql-agent pid={:?}", child.id()));
                        let new_generation = {
                            let mut inner = self.inner.lock().await;
                            inner.generation += 1;
                            inner.status_label = "Restarting".to_string();
                            inner.child = Some(ManagedChild {
                                pid: child.id(),
                                child,
                            });
                            inner.generation
                        };
                        if self.wait_for_health(settings.port).await.is_ok() {
                            let status = {
                                let mut inner = self.inner.lock().await;
                                inner.status_label = "Running".to_string();
                                inner.last_error = None;
                                self.snapshot_locked(&inner)
                            };
                            self.emit_status(Some(&app), &status);
                            generation = new_generation;
                            continue;
                        }
                    }
                    Err(err) => {
                        logging::log(format!("markql-agent respawn failed: {err}"));
                        let status = {
                            let mut inner = self.inner.lock().await;
                            inner.child = None;
                            inner.status_label = "Stopped".to_string();
                            inner.last_error = Some(err.to_string());
                            self.snapshot_locked(&inner)
                        };
                        self.emit_status(Some(&app), &status);
                        return;
                    }
                }
            }
        }
    }

    fn resolve_binary(&self, app: &Option<AppHandle>) -> Result<PathBuf, AgentError> {
        logging::log("resolving markql-agent binary");
        let mut candidates = Vec::new();
        if let Some(app_handle) = app {
            if let Ok(resource_dir) = app_handle.path().resource_dir() {
                candidates.extend(platform_binary_candidates(&resource_dir));
            }
            if let Ok(app_dir) = app_handle.path().app_data_dir() {
                candidates.extend(platform_binary_candidates(&app_dir));
            }
        }
        if let Ok(current_exe) = std::env::current_exe() {
            if let Some(exe_dir) = current_exe.parent() {
                candidates.extend(platform_binary_candidates(exe_dir));
            }
        }
        candidates.extend(platform_binary_candidates(&self.repo_root.join("desktop")));
        candidates.extend([
            self.repo_root.join("build/markql-agent"),
            self.repo_root
                .join("build/browser_plugin/agent/markql-agent"),
            self.repo_root.join("build/xsql-agent"),
            self.repo_root.join("build/browser_plugin/agent/xsql-agent"),
        ]);

        for candidate in candidates {
            if candidate.exists() {
                logging::log(format!("found markql-agent candidate {}", candidate.display()));
                return Ok(candidate);
            }
        }
        logging::log("no markql-agent candidate resolved");
        Err(AgentError::BinaryNotFound)
    }

    async fn is_healthy(&self, port: u16) -> bool {
        reqwest::Client::new()
            .get(format!("http://127.0.0.1:{port}/health"))
            .send()
            .await
            .map(|response| response.status().is_success())
            .unwrap_or(false)
    }

    async fn wait_for_health(&self, port: u16) -> Result<(), AgentError> {
        let deadline = Instant::now() + Duration::from_secs(5);
        let mut delay = Duration::from_millis(100);
        while Instant::now() < deadline {
            if self.is_healthy(port).await {
                return Ok(());
            }
            sleep(delay).await;
            delay = std::cmp::min(delay * 2, Duration::from_millis(800));
        }
        Err(AgentError::HealthTimeout)
    }
}

fn platform_binary_candidates(root: &Path) -> Vec<PathBuf> {
    #[cfg(target_os = "windows")]
    {
        let mut candidates = vec![
            root.join("binaries").join(sidecar_name()),
            root.join(sidecar_name()),
            root.join("markql-agent"),
            root.join("xsql-agent"),
        ];
        candidates.push(root.join("markql-agent.exe"));
        candidates.push(root.join("xsql-agent.exe"));
        candidates
    }

    #[cfg(not(target_os = "windows"))]
    {
        vec![
            root.join("binaries").join(sidecar_name()),
            root.join(sidecar_name()),
            root.join("markql-agent"),
            root.join("xsql-agent"),
        ]
    }
}

fn sidecar_name() -> &'static str {
    #[cfg(target_os = "linux")]
    {
        "markql-agent-x86_64-unknown-linux-gnu"
    }
    #[cfg(target_os = "windows")]
    {
        "markql-agent-x86_64-pc-windows-msvc.exe"
    }
    #[cfg(not(any(target_os = "linux", target_os = "windows")))]
    {
        "markql-agent"
    }
}

fn port_is_open(port: u16) -> bool {
    TcpStream::connect(("127.0.0.1", port)).is_ok()
}

async fn stop_child(mut managed_child: ManagedChild) -> Result<(), std::io::Error> {
    #[cfg(unix)]
    {
        if let Some(pid) = managed_child.pid {
            let _ = nix::sys::signal::kill(
                nix::unistd::Pid::from_raw(pid as i32),
                nix::sys::signal::Signal::SIGTERM,
            );
        }
    }

    if timeout(Duration::from_secs(2), managed_child.child.wait())
        .await
        .is_err()
    {
        managed_child.child.start_kill()?;
        let _ = timeout(Duration::from_secs(2), managed_child.child.wait()).await;
    }
    Ok(())
}
