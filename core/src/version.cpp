#include "xsql/version.h"

namespace xsql {

namespace {

#ifndef XSQL_VERSION
#define XSQL_VERSION "0.0.0"
#endif

#ifndef XSQL_GIT_COMMIT
#define XSQL_GIT_COMMIT "unknown"
#endif

#ifndef XSQL_GIT_DIRTY
#define XSQL_GIT_DIRTY 0
#endif

}  // namespace

VersionInfo get_version_info() {
  VersionInfo info;
  info.version = XSQL_VERSION;
  info.git_commit = XSQL_GIT_COMMIT;
  info.git_dirty = (XSQL_GIT_DIRTY != 0);
  return info;
}

std::string version_string() {
  VersionInfo info = get_version_info();
  std::string out = info.version + " (" + info.git_commit;
  if (info.git_dirty) out += "-dirty";
  out += ")";
  return out;
}

}  // namespace xsql

