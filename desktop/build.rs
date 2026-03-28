use std::env;
use std::fs;
use std::path::{Path, PathBuf};

fn main() {
    if let Err(err) = stage_sidecar() {
        panic!("{err}");
    }
    tauri_build::build();
}

fn stage_sidecar() -> Result<(), String> {
    let manifest_dir =
        PathBuf::from(env::var("CARGO_MANIFEST_DIR").map_err(|err| err.to_string())?);
    let repo_root = manifest_dir
        .parent()
        .ok_or_else(|| "desktop crate has no repository parent directory".to_string())?;
    let binaries_dir = manifest_dir.join("binaries");
    let destination = binaries_dir.join(sidecar_filename());

    println!(
        "cargo:rerun-if-changed={}",
        repo_root.join("build").display()
    );
    println!("cargo:rerun-if-changed={}", binaries_dir.display());

    if destination.exists() {
        return Ok(());
    }

    let source = sidecar_sources(repo_root)
        .into_iter()
        .find(|candidate| candidate.exists())
        .ok_or_else(|| missing_sidecar_message(&destination, repo_root))?;

    fs::create_dir_all(&binaries_dir).map_err(|err| {
        format!(
            "failed to create sidecar directory {}: {err}",
            binaries_dir.display()
        )
    })?;
    fs::copy(&source, &destination).map_err(|err| {
        format!(
            "failed to copy sidecar from {} to {}: {err}",
            source.display(),
            destination.display()
        )
    })?;

    #[cfg(unix)]
    {
        use std::os::unix::fs::PermissionsExt;
        let mut permissions = fs::metadata(&destination)
            .map_err(|err| format!("failed to stat {}: {err}", destination.display()))?
            .permissions();
        permissions.set_mode(0o755);
        fs::set_permissions(&destination, permissions).map_err(|err| {
            format!(
                "failed to mark sidecar executable at {}: {err}",
                destination.display()
            )
        })?;
    }

    Ok(())
}

fn sidecar_sources(repo_root: &Path) -> Vec<PathBuf> {
    #[cfg(target_os = "windows")]
    {
        let mut candidates = vec![
            repo_root.join("build/markql-agent"),
            repo_root.join("build/browser_plugin/agent/markql-agent"),
            repo_root.join("build/xsql-agent"),
            repo_root.join("build/browser_plugin/agent/xsql-agent"),
        ];
        candidates.splice(
            0..0,
            [
                repo_root.join("build/markql-agent.exe"),
                repo_root.join("build/browser_plugin/agent/markql-agent.exe"),
                repo_root.join("build/xsql-agent.exe"),
                repo_root.join("build/browser_plugin/agent/xsql-agent.exe"),
            ],
        );
        candidates
    }

    #[cfg(not(target_os = "windows"))]
    {
        vec![
            repo_root.join("build/markql-agent"),
            repo_root.join("build/browser_plugin/agent/markql-agent"),
            repo_root.join("build/xsql-agent"),
            repo_root.join("build/browser_plugin/agent/xsql-agent"),
        ]
    }
}

fn sidecar_filename() -> &'static str {
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

fn missing_sidecar_message(destination: &Path, repo_root: &Path) -> String {
    format!(
        "resource path `{}` doesn't exist and no built agent sidecar was found. Build the repo agent first, for example with `./scripts/build/build.sh`, so one of these exists under `{}`: markql-agent, build/browser_plugin/agent/markql-agent, xsql-agent, or build/browser_plugin/agent/xsql-agent.",
        destination.display(),
        repo_root.join("build").display()
    )
}
