#pragma once

#include <string>

namespace xsql {

/// Captures core build version and source provenance details.
/// MUST be stable and available in CLI and Python bindings.
struct VersionInfo {
  std::string version;
  std::string git_commit;
  bool git_dirty = false;
};

/// Returns compile-time version/provenance for the current core build.
/// MUST not perform IO and MUST be safe to call frequently.
VersionInfo get_version_info();
/// Returns a human-readable version + provenance string.
/// MUST include at least version and commit hash.
std::string version_string();

}  // namespace xsql

