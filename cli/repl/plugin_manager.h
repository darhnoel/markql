#pragma once

#include <unordered_map>
#include <string>
#include <vector>

#include "commands/registry.h"
#include "xsql/plugin_api.h"

namespace xsql::cli {

struct PluginInfo {
  std::string name;
  std::string path;
};

struct PluginCommandInfo {
  std::string name;
  std::string help;
  std::string plugin_name;
};

class PluginManager {
 public:
  explicit PluginManager(CommandRegistry& registry);
  bool load(const std::string& name_or_path, std::string& error);
  bool unload(const std::string& name, std::string& error);
  bool is_loaded(const std::string& name) const;
  bool tokenize(const std::string& lang,
                const std::string& text,
                std::vector<std::string>& tokens,
                std::string& error) const;
  bool has_tokenizer(const std::string& lang) const;
  const std::vector<PluginInfo>& plugins() const;
  const std::vector<PluginCommandInfo>& commands() const;

 private:
  struct LoadedPlugin {
    std::string name;
    std::string path;
    void* handle = nullptr;
  };

  struct HostContext {
    PluginManager* manager = nullptr;
    std::string current_plugin;
  };

  static bool register_command(void* host_context,
                               const char* name,
                               const char* help,
                               XsqlPluginCommandFn fn,
                               void* user_data,
                               char* out_error,
                               size_t out_error_size);
  static bool register_tokenizer(void* host_context,
                                 const char* lang,
                                 XsqlTokenizerFn fn,
                                 void* user_data,
                                 char* out_error,
                                 size_t out_error_size);
  static void print_message(void* host_context, const char* message, bool is_error);

  std::string resolve_plugin_path(const std::string& name_or_path) const;
  static bool looks_like_path(const std::string& value);

  CommandRegistry& registry_;
  HostContext host_context_;
  std::vector<LoadedPlugin> plugins_;
  std::vector<PluginInfo> plugin_info_;
  std::vector<PluginCommandInfo> command_info_;
  struct TokenizerEntry {
    XsqlTokenizerFn fn = nullptr;
    void* user_data = nullptr;
    std::string plugin_name;
  };
  std::unordered_map<std::string, TokenizerEntry> tokenizers_;
};

}  // namespace xsql::cli
