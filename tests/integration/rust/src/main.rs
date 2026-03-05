use std::env;
use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;

use anyhow::{anyhow, bail, Context, Result};
use cucumber::{given, then, when, World};

#[derive(Debug, Default, World)]
struct CliWorld {
    markql_bin: Option<PathBuf>,
    fixture_path: Option<PathBuf>,
    query: Option<String>,
    extra_args: Vec<String>,
    last_exit_code: Option<i32>,
    last_stdout: String,
    last_stderr: String,
}

impl CliWorld {
    fn repo_root() -> Result<PathBuf> {
        let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
        let root = manifest_dir
            .join("../../..")
            .canonicalize()
            .context("failed to resolve repository root from CARGO_MANIFEST_DIR")?;
        Ok(root)
    }

    fn normalize_text(text: impl Into<String>) -> String {
        text.into().replace("\r\n", "\n")
    }

    fn resolve_repo_relative(path: &str) -> Result<PathBuf> {
        let raw = PathBuf::from(path);
        if raw.is_absolute() {
            return Ok(raw);
        }
        Ok(Self::repo_root()?.join(raw))
    }

    fn resolve_markql_bin(&self) -> Result<PathBuf> {
        if let Some(path) = &self.markql_bin {
            return Ok(path.clone());
        }

        if let Ok(path) = env::var("MARKQL_BIN") {
            return Self::resolve_repo_relative(&path);
        }

        let root = Self::repo_root()?;
        let primary = root.join("build/markql");
        if primary.exists() {
            return Ok(primary);
        }

        let legacy = root.join("build/xsql");
        if legacy.exists() {
            return Ok(legacy);
        }

        Ok(primary)
    }

    fn require_fixture(&self) -> Result<&Path> {
        self.fixture_path
            .as_deref()
            .ok_or_else(|| anyhow!("fixture path was not configured"))
    }

    fn require_query(&self) -> Result<&str> {
        self.query
            .as_deref()
            .ok_or_else(|| anyhow!("query was not configured"))
    }

    fn require_exit_code(&self) -> Result<i32> {
        self.last_exit_code
            .ok_or_else(|| anyhow!("CLI has not been executed in this scenario"))
    }
}

#[given(expr = "the MarkQL binary path {string}")]
fn given_markql_binary(world: &mut CliWorld, path: String) -> Result<()> {
    let resolved = CliWorld::resolve_repo_relative(&path)?;
    world.markql_bin = Some(resolved);
    Ok(())
}

#[given(expr = "the HTML fixture {string}")]
fn given_html_fixture(world: &mut CliWorld, path: String) -> Result<()> {
    let resolved = CliWorld::resolve_repo_relative(&path)?;
    if !resolved.exists() {
        bail!("fixture does not exist: {}", resolved.display());
    }
    world.fixture_path = Some(resolved);
    Ok(())
}

#[given(expr = "the MarkQL query {string}")]
fn given_query(world: &mut CliWorld, query: String) {
    world.query = Some(query);
}

#[given(expr = "additional CLI args {string}")]
fn given_additional_args(world: &mut CliWorld, args: String) {
    world.extra_args = args
        .split_whitespace()
        .map(std::string::ToString::to_string)
        .collect();
}

#[when("I run the MarkQL CLI")]
fn when_run_markql(world: &mut CliWorld) -> Result<()> {
    let markql_bin = world.resolve_markql_bin()?;
    if !markql_bin.exists() {
        bail!(
            "markql binary not found: {} (set MARKQL_BIN or build via cmake --build build)",
            markql_bin.display()
        );
    }

    let fixture = world.require_fixture()?;
    let query = world.require_query()?;

    let mut cmd = Command::new(&markql_bin);
    cmd.args(&world.extra_args);
    cmd.arg("--query");
    cmd.arg(query);
    cmd.arg("--input");
    cmd.arg(fixture);
    cmd.arg("--color=disabled");

    let output = cmd
        .output()
        .with_context(|| format!("failed to execute markql binary: {}", markql_bin.display()))?;

    world.last_exit_code = Some(output.status.code().unwrap_or(-1));
    world.last_stdout = CliWorld::normalize_text(String::from_utf8_lossy(&output.stdout));
    world.last_stderr = CliWorld::normalize_text(String::from_utf8_lossy(&output.stderr));
    Ok(())
}

#[then(expr = "the exit code is {int}")]
fn then_exit_code(world: &mut CliWorld, expected: i32) -> Result<()> {
    let actual = world.require_exit_code()?;
    if actual != expected {
        bail!("expected exit code {}, got {}", expected, actual);
    }
    Ok(())
}

#[then(expr = "stdout contains {string}")]
fn then_stdout_contains(world: &mut CliWorld, expected: String) -> Result<()> {
    if !world.last_stdout.contains(&expected) {
        bail!(
            "stdout did not contain expected substring.\nexpected: {}\nactual stdout:\n{}",
            expected,
            world.last_stdout
        );
    }
    Ok(())
}

#[then(expr = "stderr contains {string}")]
fn then_stderr_contains(world: &mut CliWorld, expected: String) -> Result<()> {
    if !world.last_stderr.contains(&expected) {
        bail!(
            "stderr did not contain expected substring.\nexpected: {}\nactual stderr:\n{}",
            expected,
            world.last_stderr
        );
    }
    Ok(())
}

#[then("stdout is empty")]
fn then_stdout_empty(world: &mut CliWorld) -> Result<()> {
    if !world.last_stdout.trim().is_empty() {
        bail!("expected stdout to be empty, got:\n{}", world.last_stdout);
    }
    Ok(())
}

#[then("stderr is empty")]
fn then_stderr_empty(world: &mut CliWorld) -> Result<()> {
    if !world.last_stderr.trim().is_empty() {
        bail!("expected stderr to be empty, got:\n{}", world.last_stderr);
    }
    Ok(())
}

#[then(expr = "stdout matches golden file {string}")]
fn then_stdout_matches_golden(world: &mut CliWorld, path: String) -> Result<()> {
    let golden_path = CliWorld::resolve_repo_relative(&path)?;
    let golden = fs::read_to_string(&golden_path)
        .with_context(|| format!("failed to read golden file: {}", golden_path.display()))?;

    let expected = CliWorld::normalize_text(golden);
    if world.last_stdout != expected {
        bail!(
            "stdout mismatch with golden file {}\n--- expected ---\n{}\n--- actual ---\n{}",
            golden_path.display(),
            expected,
            world.last_stdout
        );
    }
    Ok(())
}

#[tokio::main]
async fn main() {
    let features = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../features");
    CliWorld::run(features).await;
}
