#pragma once

#include <string>
#include <vector>

namespace xsql::cli {

struct PluginRegistryEntry {
  std::string name;
  std::string repo;
  std::string path;
  std::string artifact;
};

std::string plugin_registry_path();
bool load_plugin_registry(std::vector<PluginRegistryEntry>& entries, std::string& error);
const PluginRegistryEntry* find_plugin_entry(const std::vector<PluginRegistryEntry>& entries,
                                             const std::string& name);

}  // namespace xsql::cli
