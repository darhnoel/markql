use std::fs;
use std::path::Path;
use std::process::Command;

use anyhow::{bail, Context, Result};

pub fn load_input(input: &str) -> Result<String> {
    if is_url(input) {
        fetch_url(input)
    } else {
        read_file(input)
    }
}

fn is_url(input: &str) -> bool {
    input.starts_with("http://") || input.starts_with("https://")
}

fn fetch_url(url: &str) -> Result<String> {
    let output = Command::new("curl")
        .args(["-LfsS", url])
        .output()
        .with_context(|| format!("failed to execute curl for URL: {url}"))?;
    if !output.status.success() {
        let stderr = String::from_utf8_lossy(&output.stderr);
        let message = stderr.trim();
        if message.is_empty() {
            bail!("curl failed for URL: {url}");
        }
        bail!("curl failed for URL {url}: {message}");
    }
    String::from_utf8(output.stdout).context("curl returned non-UTF-8 content")
}

fn read_file(path: &str) -> Result<String> {
    let path_ref = Path::new(path);
    fs::read_to_string(path_ref)
        .with_context(|| format!("failed to read file: {}", path_ref.display()))
}
