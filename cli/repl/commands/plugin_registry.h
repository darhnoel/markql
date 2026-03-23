#pragma once

#include <string>
#include <vector>

namespace markql::cli {

struct PluginRegistryEntry {
  std::string name;
  std::string repo;
  std::string path;
  std::string artifact;
};

bool is_safe_plugin_identifier(const std::string& value);
bool validate_plugin_registry_entry(const PluginRegistryEntry& entry, std::string& error);

std::string plugin_registry_path();
bool load_plugin_registry(std::vector<PluginRegistryEntry>& entries, std::string& error);
const PluginRegistryEntry* find_plugin_entry(const std::vector<PluginRegistryEntry>& entries,
                                             const std::string& name);

}  // namespace markql::cli
