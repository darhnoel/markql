#include "plugin_command.h"

#include <iostream>
#include <sstream>
#include <filesystem>

#include "../plugin_manager.h"
#include "plugin_install_command.h"
#include "plugin_registry.h"

namespace xsql::cli {
namespace {

std::string shared_library_suffix() {
#if defined(_WIN32)
  return ".dll";
#elif defined(__APPLE__)
  return ".dylib";
#else
  return ".so";
#endif
}

std::string resolve_artifact_name(const PluginRegistryEntry* entry,
                                  const std::string& name) {
  std::string artifact;
  if (entry && !entry->artifact.empty()) {
    artifact = entry->artifact;
  } else {
    artifact = "lib" + name + "{ext}";
  }
  size_t pos = artifact.find("{ext}");
  if (pos != std::string::npos) {
    artifact.replace(pos, 5, shared_library_suffix());
  }
  return artifact;
}

}  // namespace

CommandHandler make_plugin_command() {
  return [](const std::string& line, CommandContext& ctx) -> bool {
    if (line.rfind(".plugin", 0) != 0) {
      return false;
    }
    if (line.size() > 7) {
      char next = line[7];
      if (!std::isspace(static_cast<unsigned char>(next))) {
        return false;
      }
    }
    std::istringstream iss(line);
    std::string cmd;
    iss >> cmd;
    std::string subcmd;
    iss >> subcmd;
    if (subcmd.empty()) {
      std::cerr << "Usage: .plugin list | .plugin load <name|path> | .plugin unload <name>\n"
                   "       .plugin install <name> | .plugin remove <name>" << std::endl;
      return true;
    }
    if (subcmd == "list") {
      const auto& plugins = ctx.plugin_manager.plugins();
      if (plugins.empty()) {
        std::cout << "No plugins loaded." << std::endl;
      } else {
        std::cout << "Loaded plugins:" << std::endl;
        for (const auto& plugin : plugins) {
          std::cout << "  " << plugin.name << " (" << plugin.path << ")" << std::endl;
        }
      }
      std::vector<PluginRegistryEntry> entries;
      std::string error;
      if (load_plugin_registry(entries, error)) {
        if (!entries.empty()) {
          std::cout << "Available plugins:" << std::endl;
          for (const auto& entry : entries) {
            std::cout << "  " << entry.name << " (" << entry.repo << ")" << std::endl;
          }
        }
      }
      return true;
    }
    if (subcmd == "load") {
      std::string name;
      iss >> name;
      if (name.empty()) {
        std::cerr << "Usage: .plugin load <name|path>" << std::endl;
        return true;
      }
      std::string error;
      if (!ctx.plugin_manager.load(name, error)) {
        std::cerr << "Error: " << error << std::endl;
        return true;
      }
      std::cout << "Loaded plugin: " << name << std::endl;
      return true;
    }
    if (subcmd == "unload") {
      std::string name;
      iss >> name;
      if (name.empty()) {
        std::cerr << "Usage: .plugin unload <name>" << std::endl;
        return true;
      }
      std::string error;
      if (!ctx.plugin_manager.unload(name, error)) {
        std::cerr << "Error: " << error << std::endl;
        return true;
      }
      std::cout << "Unloaded plugin: " << name << std::endl;
      return true;
    }
    if (subcmd == "install") {
      std::string name;
      iss >> name;
      if (name.empty()) {
        std::cerr << "Usage: .plugin install <name> [--verbose]" << std::endl;
        return true;
      }
      std::string rest;
      std::getline(iss, rest);
      auto installer = make_plugin_install_command();
      return installer(".plugin_install " + name + rest, ctx);
    }
    if (subcmd == "remove") {
      std::string name;
      iss >> name;
      if (name.empty()) {
        std::cerr << "Usage: .plugin remove <name>" << std::endl;
        return true;
      }
      std::string error;
      ctx.plugin_manager.unload(name, error);

      std::vector<PluginRegistryEntry> entries;
      const PluginRegistryEntry* entry = nullptr;
      std::string registry_error;
      if (load_plugin_registry(entries, registry_error)) {
        entry = find_plugin_entry(entries, name);
      }
      std::filesystem::path plugin_root = std::filesystem::path("plugins") / "src" / name;
      if (std::filesystem::exists(plugin_root)) {
        std::filesystem::remove_all(plugin_root);
      }
      std::filesystem::path bin_root = std::filesystem::path("plugins") / "bin";
      std::filesystem::path artifact = resolve_artifact_name(entry, name);
      std::filesystem::path artifact_path = bin_root / artifact;
      if (std::filesystem::exists(artifact_path)) {
        std::filesystem::remove(artifact_path);
      }
      std::cout << "Removed plugin: " << name << std::endl;
      return true;
    }
    std::cerr << "Unknown subcommand: " << subcmd << std::endl;
    std::cerr << "Usage: .plugin list | .plugin load <name|path> | .plugin unload <name>\n"
                 "       .plugin install <name> | .plugin remove <name>" << std::endl;
    return true;
  };
}

}  // namespace xsql::cli
