#include "markql_internal.h"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

#ifdef MARKQL_USE_CURL
#include <curl/curl.h>
#endif

namespace markql::markql_internal {

/// Loads file contents for core query execution.
/// MUST throw on IO errors and MUST not perform network access.
/// Inputs are paths; outputs are contents with file IO side effects.
std::string read_file(const std::string& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("Failed to open file: " + path);
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

#ifdef MARKQL_USE_CURL
namespace {

void ensure_curl_global_init() {
  static const auto init_result = []() {
    return curl_global_init(CURL_GLOBAL_DEFAULT);
  }();
  if (init_result != CURLE_OK) {
    throw std::runtime_error("Failed to initialize curl globals");
  }
}

std::string shell_quote_single(const std::string& value) {
  std::string quoted;
  quoted.reserve(value.size() + 2);
  quoted.push_back('\'');
  for (char c : value) {
    if (c == '\'') {
      quoted += "'\\''";
    } else {
      quoted.push_back(c);
    }
  }
  quoted.push_back('\'');
  return quoted;
}

std::string run_command_capture(const std::string& command) {
#ifdef _WIN32
  (void)command;
  throw std::runtime_error("System curl fallback is not supported on Windows");
#else
  FILE* pipe = popen(command.c_str(), "r");
  if (!pipe) {
    throw std::runtime_error("Failed to start system curl fallback");
  }
  std::string output;
  char buffer[8192];
  while (true) {
    size_t read = std::fread(buffer, 1, sizeof(buffer), pipe);
    if (read > 0) {
      output.append(buffer, read);
    }
    if (read < sizeof(buffer)) {
      if (std::feof(pipe)) break;
      if (std::ferror(pipe)) {
        pclose(pipe);
        throw std::runtime_error("Failed to read system curl output");
      }
    }
  }
  int status = pclose(pipe);
  if (status != 0) {
    throw std::runtime_error("System curl fallback failed");
  }
  return output;
#endif
}

bool looks_like_browser_challenge_page(const std::string& html) {
  return html.find("Just a moment...") != std::string::npos ||
         html.find("Enable JavaScript and cookies to continue") != std::string::npos ||
         html.find("_cf_chl_opt") != std::string::npos ||
         html.find("/cdn-cgi/challenge-platform/") != std::string::npos;
}

std::string fetch_url_with_system_curl(const std::string& url, int timeout_ms) {
  int timeout_seconds = timeout_ms <= 0 ? 5 : (timeout_ms + 999) / 1000;
  std::ostringstream command;
  command << "curl -LfsS --compressed --max-time " << timeout_seconds << " "
          << shell_quote_single(url);
  return run_command_capture(command.str());
}

/// Appends curl response bytes into a caller-provided buffer.
/// MUST return the full byte count or curl treats it as an error.
/// Inputs are raw buffers; side effects include buffer writes.
size_t write_to_string(void* contents, size_t size, size_t nmemb, void* userp) {
  size_t total = size * nmemb;
  auto* out = static_cast<std::string*>(userp);
  out->append(static_cast<const char*>(contents), total);
  return total;
}

std::string normalize_content_type(const char* raw) {
  if (!raw) return "";
  std::string value(raw);
  size_t end = value.find(';');
  if (end != std::string::npos) {
    value = value.substr(0, end);
  }
  size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start]))) {
    ++start;
  }
  size_t finish = value.size();
  while (finish > start && std::isspace(static_cast<unsigned char>(value[finish - 1]))) {
    --finish;
  }
  value = value.substr(start, finish - start);
  for (char& c : value) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

void validate_content_type(CURL* curl) {
  const char* raw = nullptr;
  CURLcode info = curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &raw);
  if (info != CURLE_OK) {
    throw std::runtime_error("Failed to read Content-Type for URL");
  }
  std::string content_type = normalize_content_type(raw);
  if (content_type.empty()) {
    throw std::runtime_error("Missing Content-Type for URL");
  }
  if (content_type == "text/html" ||
      content_type == "application/xhtml+xml" ||
      content_type == "application/xml" ||
      content_type == "text/xml") {
    return;
  }
  throw std::runtime_error("Unsupported Content-Type for HTML fetch: " + content_type);
}

}  // namespace
#endif

/// Fetches URL content for core query execution when curl is enabled.
/// MUST honor timeout_ms and MUST throw on curl failures.
/// Inputs are URL/timeout; outputs are contents with network side effects.
std::string fetch_url(const std::string& url, int timeout_ms) {
#ifdef MARKQL_USE_CURL
  ensure_curl_global_init();
  CURL* curl = curl_easy_init();
  if (!curl) {
    throw std::runtime_error("Failed to initialize curl");
  }
  std::string buffer;
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_to_string);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);
  // WHY: a stable user agent helps servers classify client behavior.
  curl_easy_setopt(curl, CURLOPT_USERAGENT, "markql/0.1");
  CURLcode res = curl_easy_perform(curl);
  if (res != CURLE_OK) {
    curl_easy_cleanup(curl);
    throw std::runtime_error(std::string("Failed to fetch URL: ") + curl_easy_strerror(res));
  }
  validate_content_type(curl);
  curl_easy_cleanup(curl);
  if (looks_like_browser_challenge_page(buffer)) {
    try {
      std::string fallback = fetch_url_with_system_curl(url, timeout_ms);
      if (!fallback.empty() && !looks_like_browser_challenge_page(fallback)) {
        return fallback;
      }
    } catch (const std::exception&) {
      // WHY: preserve the libcurl result if system curl fallback is unavailable.
    }
  }
  return buffer;
#else
  (void)url;
  (void)timeout_ms;
  throw std::runtime_error("URL fetching is disabled (libcurl not available)");
#endif
}

}  // namespace markql::markql_internal
