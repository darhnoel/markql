const ui = {
  statusLabel: document.querySelector("#statusLabel"),
  statusMeta: document.querySelector("#statusMeta"),
  feedback: document.querySelector("#feedback"),
  tokenInput: document.querySelector("#tokenInput"),
  startOnLoginInput: document.querySelector("#startOnLoginInput"),
  timeoutInput: document.querySelector("#timeoutInput"),
  portInput: document.querySelector("#portInput"),
  startBtn: document.querySelector("#startBtn"),
  stopBtn: document.querySelector("#stopBtn"),
  copyTokenBtn: document.querySelector("#copyTokenBtn"),
  regenerateTokenBtn: document.querySelector("#regenerateTokenBtn"),
  saveBtn: document.querySelector("#saveBtn")
};

function setFeedback(message, isError = false) {
  ui.feedback.textContent = message;
  ui.feedback.classList.toggle("error", isError);
}

const tauri = window.__TAURI__;
const invoke = tauri && tauri.core ? tauri.core.invoke : null;
const listen = tauri && tauri.event ? tauri.event.listen : null;

if (!invoke || !listen) {
  setFeedback("Tauri bridge is not available. Enable app.withGlobalTauri or use a bundled frontend API import.", true);
  throw new Error("window.__TAURI__ bridge is unavailable");
}

function applySettings(settings) {
  ui.tokenInput.value = settings.token || "";
  ui.startOnLoginInput.checked = Boolean(settings.start_on_login);
  ui.timeoutInput.value = String(settings.query_timeout_ms || 5000);
  ui.portInput.value = String(settings.port || 7337);
}

function applyStatus(status) {
  ui.statusLabel.textContent = status.label;
  const details = [`Port ${status.port}`];
  if (status.managed_process) {
    details.push("managed");
  }
  if (status.restart_attempts > 0) {
    details.push(`restart ${status.restart_attempts}/3`);
  }
  if (status.last_error) {
    details.push(status.last_error);
  }
  ui.statusMeta.textContent = details.join(" · ");
}

async function refresh() {
  const [settings, status] = await Promise.all([
    invoke("get_settings"),
    invoke("get_status")
  ]);
  applySettings(settings);
  applyStatus(status);
}

async function saveSettings() {
  const settings = {
    token: ui.tokenInput.value.trim(),
    start_on_login: ui.startOnLoginInput.checked,
    query_timeout_ms: Number(ui.timeoutInput.value || 5000),
    port: Number(ui.portInput.value || 7337)
  };
  const saved = await invoke("save_settings", { settings });
  applySettings(saved);
  setFeedback("Settings saved.");
}

ui.startBtn.addEventListener("click", async () => {
  try {
    const status = await invoke("start_agent");
    applyStatus(status);
    setFeedback("Agent started.");
  } catch (error) {
    setFeedback(String(error), true);
  }
});

ui.stopBtn.addEventListener("click", async () => {
  try {
    const status = await invoke("stop_agent");
    applyStatus(status);
    setFeedback("Agent stopped.");
  } catch (error) {
    setFeedback(String(error), true);
  }
});

ui.copyTokenBtn.addEventListener("click", async () => {
  try {
    await invoke("copy_token");
    setFeedback("Token copied to clipboard.");
  } catch (error) {
    setFeedback(String(error), true);
  }
});

ui.regenerateTokenBtn.addEventListener("click", async () => {
  try {
    const settings = await invoke("regenerate_token");
    applySettings(settings);
    setFeedback("Generated a new token.");
  } catch (error) {
    setFeedback(String(error), true);
  }
});

ui.saveBtn.addEventListener("click", async () => {
  try {
    await saveSettings();
  } catch (error) {
    setFeedback(String(error), true);
  }
});

listen("agent-status", (event) => {
  applyStatus(event.payload);
});

refresh().catch((error) => {
  setFeedback(String(error), true);
});
