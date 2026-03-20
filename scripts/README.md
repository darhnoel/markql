# Script Layout

Canonical script implementations live in categorized subdirectories under `scripts/`.

The repository root no longer carries convenience script wrappers.

Compatibility wrappers remain at:
- `scripts/*.sh` for older flat `scripts/` paths such as `./scripts/package_deb.sh`

Use the categorized paths for new references:

- `scripts/build/build.sh`
- `scripts/agent/start-agent.sh`
- `scripts/cli/markqli.sh`
- `scripts/install/install_markql.sh`
- `scripts/install/uninstall_markql.sh`
- `scripts/python/install.sh`
- `scripts/python/test.sh`
- `scripts/test/ctest.sh`
- `scripts/package/package_deb.sh`
- `scripts/package/package_appimage.sh`
- `scripts/tools/run_rust_inspector.sh`
- `scripts/tools/show_logo.sh`
- `scripts/maintenance/verify_markql_rename.sh`

This keeps the repository root stable for existing workflows while making script ownership and purpose easier to scan.
