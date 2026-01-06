#include "plugin_registry.h"

#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace xsql::cli {
namespace {

std::string trim(const std::string& value) {
  size_t start = value.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return "";
  }
  size_t end = value.find_last_not_of(" \t\r\n");
  return value.substr(start, end - start + 1);
}

struct JsonCursor {
  const std::string& input;
  size_t pos = 0;
};

void skip_ws(JsonCursor& cur) {
  while (cur.pos < cur.input.size() &&
         std::isspace(static_cast<unsigned char>(cur.input[cur.pos]))) {
    ++cur.pos;
  }
}

bool consume_char(JsonCursor& cur, char ch) {
  skip_ws(cur);
  if (cur.pos >= cur.input.size() || cur.input[cur.pos] != ch) {
    return false;
  }
  ++cur.pos;
  return true;
}

bool parse_string(JsonCursor& cur, std::string& out) {
  skip_ws(cur);
  if (cur.pos >= cur.input.size() || cur.input[cur.pos] != '"') {
    return false;
  }
  ++cur.pos;
  std::string result;
  while (cur.pos < cur.input.size()) {
    char ch = cur.input[cur.pos++];
    if (ch == '"') {
      out = result;
      return true;
    }
    if (ch == '\\') {
      if (cur.pos >= cur.input.size()) {
        return false;
      }
      char esc = cur.input[cur.pos++];
      switch (esc) {
        case '"':
        case '\\':
        case '/':
          result.push_back(esc);
          break;
        case 'b':
          result.push_back('\b');
          break;
        case 'f':
          result.push_back('\f');
          break;
        case 'n':
          result.push_back('\n');
          break;
        case 'r':
          result.push_back('\r');
          break;
        case 't':
          result.push_back('\t');
          break;
        case 'u': {
          if (cur.pos + 4 > cur.input.size()) {
            return false;
          }
          cur.pos += 4;
          result.push_back('?');
          break;
        }
        default:
          return false;
      }
    } else {
      result.push_back(ch);
    }
  }
  return false;
}

bool parse_registry_json(const std::string& json,
                         std::vector<PluginRegistryEntry>& entries,
                         std::string& error) {
  JsonCursor cur{json, 0};
  if (!consume_char(cur, '[')) {
    error = "Registry must be a JSON array.";
    return false;
  }
  skip_ws(cur);
  if (consume_char(cur, ']')) {
    return true;
  }
  while (cur.pos < cur.input.size()) {
    if (!consume_char(cur, '{')) {
      error = "Expected object in registry array.";
      return false;
    }
    PluginRegistryEntry entry;
    bool done = false;
    while (!done) {
      std::string key;
      if (!parse_string(cur, key)) {
        error = "Invalid object key in registry.";
        return false;
      }
      if (!consume_char(cur, ':')) {
        error = "Expected ':' after key.";
        return false;
      }
      std::string value;
      if (!parse_string(cur, value)) {
        error = "Expected string value for key: " + key;
        return false;
      }
      if (key == "name") {
        entry.name = value;
      } else if (key == "repo") {
        entry.repo = value;
      } else if (key == "path" || key == "cmake_subdir") {
        entry.path = value;
      } else if (key == "artifact") {
        entry.artifact = value;
      }
      skip_ws(cur);
      if (consume_char(cur, ',')) {
        continue;
      }
      if (consume_char(cur, '}')) {
        done = true;
        break;
      }
      error = "Expected ',' or '}' in registry object.";
      return false;
    }
    if (entry.name.empty() || entry.repo.empty()) {
      error = "Registry entry missing required fields (name, repo).";
      return false;
    }
    if (entry.path.empty()) {
      entry.path = ".";
    }
    entries.push_back(std::move(entry));
    skip_ws(cur);
    if (consume_char(cur, ',')) {
      continue;
    }
    if (consume_char(cur, ']')) {
      return true;
    }
    error = "Expected ',' or ']' after registry object.";
    return false;
  }
  error = "Unexpected end of registry.";
  return false;
}

}  // namespace

std::string plugin_registry_path() {
  std::string path = "plugins/registry.json";
  if (const char* env = std::getenv("XSQL_PLUGIN_REGISTRY")) {
    std::string trimmed = trim(env);
    if (!trimmed.empty()) {
      path = trimmed;
    }
  }
  return path;
}

bool load_plugin_registry(std::vector<PluginRegistryEntry>& entries, std::string& error) {
  std::string path = plugin_registry_path();
  std::ifstream in(path);
  if (!in) {
    error = "Plugin registry not found: " + path;
    return false;
  }
  std::stringstream buffer;
  buffer << in.rdbuf();
  std::string json = buffer.str();
  return parse_registry_json(json, entries, error);
}

const PluginRegistryEntry* find_plugin_entry(const std::vector<PluginRegistryEntry>& entries,
                                             const std::string& name) {
  for (const auto& entry : entries) {
    if (entry.name == name) {
      return &entry;
    }
  }
  return nullptr;
}

}  // namespace xsql::cli
