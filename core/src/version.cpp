#include "markql/version.h"

namespace markql {

namespace {

#ifndef MARKQL_VERSION
#define MARKQL_VERSION "0.0.0"
#endif

#ifndef MARKQL_GIT_COMMIT
#define MARKQL_GIT_COMMIT "unknown"
#endif

#ifndef MARKQL_GIT_DIRTY
#define MARKQL_GIT_DIRTY 0
#endif

}  // namespace

VersionInfo get_version_info() {
  VersionInfo info;
  info.version = MARKQL_VERSION;
  info.git_commit = MARKQL_GIT_COMMIT;
  info.git_dirty = (MARKQL_GIT_DIRTY != 0);
  return info;
}

std::string version_string() {
  VersionInfo info = get_version_info();
  std::string out = info.version + " (" + info.git_commit;
  if (info.git_dirty) out += "-dirty";
  out += ")";
  return out;
}

}  // namespace markql

