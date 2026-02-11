#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "sha256.h"
#include "xsql/xsql.h"

namespace {

using json = nlohmann::json;

constexpr const char* kAgentVersion = "0.1.0";
constexpr const char* kBindHost = "127.0.0.1";
constexpr int kBindPort = 7337;
constexpr size_t kDefaultMaxRows = 2000;
constexpr int kDefaultTimeoutMs = 5000;
constexpr size_t kMaxSnapshotCacheEntries = 8;

struct QueryOptions {
  size_t max_rows = kDefaultMaxRows;
  int timeout_ms = kDefaultTimeoutMs;
};

struct QueryResponse {
  int elapsed_ms = 0;
  json columns = json::array();
  json rows = json::array();
  bool truncated = false;
  json error = nullptr;
};

struct ExecutionOutcome {
  bool ok = false;
  bool timed_out = false;
  xsql::QueryResult result;
  std::string error_message;
};

class SnapshotCache {
 public:
  struct Entry {
    std::string sha256;
    std::shared_ptr<const xsql::ParsedDocumentHandle> prepared;
    std::chrono::steady_clock::time_point last_access;
  };

  explicit SnapshotCache(size_t max_entries) : max_entries_(max_entries) {}

  Entry get_or_insert(const std::string& html) {
    const std::string digest = xsql::agent::sha256::digest_hex(html);
    {
      std::lock_guard<std::mutex> lock(mu_);
      auto it = entries_.find(digest);
      const auto now = std::chrono::steady_clock::now();
      if (it != entries_.end()) {
        it->second.last_access = now;
        return it->second;
      }
    }

    std::shared_ptr<const xsql::ParsedDocumentHandle> prepared =
        xsql::prepare_document(html, "document");

    std::lock_guard<std::mutex> lock(mu_);
    const auto now = std::chrono::steady_clock::now();
    auto it = entries_.find(digest);
    if (it != entries_.end()) {
      it->second.last_access = now;
      return it->second;
    }
    if (entries_.size() >= max_entries_) {
      evict_lru();
    }
    Entry entry{digest, std::move(prepared), now};
    entries_[digest] = entry;
    return entry;
  }

 private:
  void evict_lru() {
    if (entries_.empty()) return;
    auto oldest = entries_.begin();
    for (auto it = entries_.begin(); it != entries_.end(); ++it) {
      if (it->second.last_access < oldest->second.last_access) {
        oldest = it;
      }
    }
    entries_.erase(oldest);
  }

  size_t max_entries_;
  std::mutex mu_;
  std::unordered_map<std::string, Entry> entries_;
};

class IXsqlExecutor {
 public:
  virtual ~IXsqlExecutor() = default;
  virtual ExecutionOutcome execute(const std::shared_ptr<const xsql::ParsedDocumentHandle>& prepared,
                                   const std::string& query,
                                   int timeout_ms) = 0;
};

class CoreExecutor final : public IXsqlExecutor {
 public:
  ExecutionOutcome execute(const std::shared_ptr<const xsql::ParsedDocumentHandle>& prepared,
                           const std::string& query,
                           int timeout_ms) override {
    if (timeout_ms <= 0) {
      timeout_ms = kDefaultTimeoutMs;
    }

    std::promise<ExecutionOutcome> promise;
    std::future<ExecutionOutcome> future = promise.get_future();

    std::thread worker([prepared, query, p = std::move(promise)]() mutable {
      ExecutionOutcome outcome;
      try {
        outcome.ok = true;
        outcome.result = xsql::execute_query_from_prepared_document(prepared, query);
      } catch (const std::exception& ex) {
        outcome.ok = false;
        outcome.error_message = ex.what();
      }
      try {
        p.set_value(std::move(outcome));
      } catch (...) {
      }
    });

    const auto status = future.wait_for(std::chrono::milliseconds(timeout_ms));
    if (status == std::future_status::ready) {
      worker.join();
      return future.get();
    }

    worker.detach();
    ExecutionOutcome timeout;
    timeout.ok = false;
    timeout.timed_out = true;
    timeout.error_message = "Query timed out after " + std::to_string(timeout_ms) + " ms";
    return timeout;
  }
};

int elapsed_ms(const std::chrono::steady_clock::time_point start) {
  const auto elapsed = std::chrono::steady_clock::now() - start;
  return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
}

std::string generate_token() {
  std::random_device rd;
  std::uniform_int_distribution<int> dist(0, 255);
  std::string out;
  out.reserve(64);
  const char* hex = "0123456789abcdef";
  for (int i = 0; i < 32; ++i) {
    const uint8_t value = static_cast<uint8_t>(dist(rd));
    out.push_back(hex[(value >> 4U) & 0x0fU]);
    out.push_back(hex[value & 0x0fU]);
  }
  return out;
}

std::string resolve_token() {
  const char* env = std::getenv("XSQL_AGENT_TOKEN");
  if (env != nullptr && env[0] != '\0') {
    return std::string(env);
  }
  const std::string generated = generate_token();
  std::cout << "[xsql-agent] XSQL_AGENT_TOKEN not set. Generated token:\n"
            << generated << "\n"
            << "[xsql-agent] Copy this token into the extension settings.\n";
  return generated;
}

void set_cors_headers(httplib::Response& res) {
  res.set_header("Access-Control-Allow-Origin", "*");
  res.set_header("Access-Control-Allow-Methods", "POST, OPTIONS");
  res.set_header("Access-Control-Allow-Headers", "Content-Type, X-XSQL-Token");
}

void write_json(httplib::Response& res, int status, const json& payload) {
  res.status = status;
  set_cors_headers(res);
  res.set_content(payload.dump(), "application/json");
}

json build_error(int elapsed, const std::string& code, const std::string& message) {
  return json{
      {"elapsed_ms", elapsed},
      {"columns", json::array()},
      {"rows", json::array()},
      {"truncated", false},
      {"error", {{"code", code}, {"message", message}}},
  };
}

bool parse_request(const httplib::Request& req,
                   std::string& html,
                   std::string& query,
                   QueryOptions& options,
                   std::string& error) {
  const json body = json::parse(req.body, nullptr, false);
  if (body.is_discarded() || !body.is_object()) {
    error = "Request body must be a JSON object";
    return false;
  }
  if (!body.contains("html") || !body["html"].is_string()) {
    error = "Field 'html' is required and must be a string";
    return false;
  }
  if (!body.contains("query") || !body["query"].is_string()) {
    error = "Field 'query' is required and must be a string";
    return false;
  }

  html = body["html"].get<std::string>();
  query = body["query"].get<std::string>();

  if (body.contains("options") && body["options"].is_object()) {
    const json& opts = body["options"];
    if (opts.contains("max_rows") && opts["max_rows"].is_number_integer()) {
      const auto parsed = opts["max_rows"].get<int64_t>();
      if (parsed > 0) {
        options.max_rows = static_cast<size_t>(parsed);
      }
    }
    if (opts.contains("timeout_ms") && opts["timeout_ms"].is_number_integer()) {
      const auto parsed = opts["timeout_ms"].get<int64_t>();
      if (parsed > 0 && parsed <= 120000) {
        options.timeout_ms = static_cast<int>(parsed);
      }
    }
  }

  if (query.empty()) {
    error = "Field 'query' must not be empty";
    return false;
  }
  return true;
}

std::vector<std::string> resolve_columns(const xsql::QueryResult& result) {
  if (!result.columns.empty()) {
    return result.columns;
  }
  return {"node_id", "tag", "attributes", "parent_id", "max_depth", "doc_order"};
}

json value_for_field(const std::string& field, const xsql::QueryResultRow& row) {
  if (field == "node_id" || field == "count") return row.node_id;
  if (field == "tag") return row.tag;
  if (field == "text") return row.text;
  if (field == "inner_html") return row.inner_html;
  if (field == "parent_id") {
    if (row.parent_id.has_value()) return *row.parent_id;
    return nullptr;
  }
  if (field == "sibling_pos") return row.sibling_pos;
  if (field == "max_depth") return row.max_depth;
  if (field == "doc_order") return row.doc_order;
  if (field == "source_uri") return row.source_uri;
  if (field == "attributes") {
    json attrs = json::object();
    for (const auto& kv : row.attributes) {
      attrs[kv.first] = kv.second;
    }
    return attrs;
  }
  if (field == "terms_score") {
    json scores = json::object();
    for (const auto& kv : row.term_scores) {
      scores[kv.first] = kv.second;
    }
    return scores;
  }

  const auto computed = row.computed_fields.find(field);
  if (computed != row.computed_fields.end()) {
    return computed->second;
  }
  const auto attr = row.attributes.find(field);
  if (attr != row.attributes.end()) {
    return attr->second;
  }
  return nullptr;
}

std::string infer_column_type(const std::string& field) {
  if (field == "node_id" || field == "count" || field == "parent_id" ||
      field == "sibling_pos" || field == "max_depth" || field == "doc_order") {
    return "number";
  }
  if (field == "attributes" || field == "terms_score") {
    return "object";
  }
  return "string";
}

QueryResponse map_row_result(const xsql::QueryResult& result, size_t max_rows) {
  QueryResponse out;
  const std::vector<std::string> columns = resolve_columns(result);

  for (const auto& name : columns) {
    out.columns.push_back({{"name", name}, {"type", infer_column_type(name)}});
  }

  for (const auto& row : result.rows) {
    if (out.rows.size() >= max_rows) {
      out.truncated = true;
      break;
    }
    json values = json::array();
    for (const auto& field : columns) {
      values.push_back(value_for_field(field, row));
    }
    out.rows.push_back(std::move(values));
  }
  return out;
}

QueryResponse map_list_result(const xsql::QueryResult& result, size_t max_rows) {
  QueryResponse out;
  std::string field = "value";
  if (!result.columns.empty()) {
    field = result.columns.front();
  }

  out.columns.push_back({{"name", field}, {"type", infer_column_type(field)}});
  for (const auto& row : result.rows) {
    if (out.rows.size() >= max_rows) {
      out.truncated = true;
      break;
    }
    out.rows.push_back(json::array({value_for_field(field, row)}));
  }
  return out;
}

QueryResponse map_table_result(const xsql::QueryResult& result, size_t max_rows) {
  QueryResponse out;
  if (result.tables.empty()) {
    return out;
  }

  bool include_table_id = result.tables.size() > 1;
  std::vector<std::string> header;
  size_t max_cols = 0;

  for (const auto& table : result.tables) {
    if (table.rows.empty()) continue;
    if (result.table_has_header && header.empty()) {
      header = table.rows.front();
    }
    size_t start = result.table_has_header ? 1 : 0;
    for (size_t i = start; i < table.rows.size(); ++i) {
      max_cols = std::max(max_cols, table.rows[i].size());
    }
  }

  if (max_cols == 0 && !header.empty()) {
    max_cols = header.size();
  }

  if (include_table_id) {
    out.columns.push_back({{"name", "table_node_id"}, {"type", "number"}});
  }

  for (size_t i = 0; i < max_cols; ++i) {
    std::string name;
    if (i < header.size() && !header[i].empty()) {
      name = header[i];
    } else {
      name = "col_" + std::to_string(i + 1);
    }
    out.columns.push_back({{"name", name}, {"type", "string"}});
  }

  for (const auto& table : result.tables) {
    size_t start = result.table_has_header ? 1 : 0;
    for (size_t i = start; i < table.rows.size(); ++i) {
      if (out.rows.size() >= max_rows) {
        out.truncated = true;
        return out;
      }

      json values = json::array();
      if (include_table_id) {
        values.push_back(table.node_id);
      }
      for (size_t c = 0; c < max_cols; ++c) {
        if (c < table.rows[i].size()) {
          values.push_back(table.rows[i][c]);
        } else {
          values.push_back(nullptr);
        }
      }
      out.rows.push_back(std::move(values));
    }
  }

  return out;
}

QueryResponse map_result(const xsql::QueryResult& result, size_t max_rows) {
  if (result.to_table) {
    return map_table_result(result, max_rows);
  }
  if (result.to_list) {
    return map_list_result(result, max_rows);
  }
  return map_row_result(result, max_rows);
}

}  // namespace

int main() {
  const std::string token = resolve_token();
  SnapshotCache cache(kMaxSnapshotCacheEntries);
  CoreExecutor executor;

  httplib::Server server;

  server.Get("/health", [](const httplib::Request&, httplib::Response& res) {
    write_json(res, 200, {{"ok", true}, {"agent_version", kAgentVersion}});
  });

  server.Options("/v1/query", [](const httplib::Request&, httplib::Response& res) {
    res.status = 204;
    set_cors_headers(res);
  });

  server.Post("/v1/query", [&](const httplib::Request& req, httplib::Response& res) {
    const auto started_at = std::chrono::steady_clock::now();

    const std::string provided_token = req.get_header_value("X-XSQL-Token");
    if (provided_token.empty() || provided_token != token) {
      const json error = build_error(elapsed_ms(started_at),
                                     "UNAUTHORIZED",
                                     "Missing or invalid X-XSQL-Token");
      write_json(res, 401, error);
      return;
    }

    QueryOptions options;
    std::string html;
    std::string query;
    std::string parse_error;
    if (!parse_request(req, html, query, options, parse_error)) {
      const json error = build_error(elapsed_ms(started_at), "BAD_REQUEST", parse_error);
      write_json(res, 400, error);
      return;
    }

    SnapshotCache::Entry snapshot;
    try {
      snapshot = cache.get_or_insert(html);
    } catch (const std::exception& ex) {
      const json error = build_error(elapsed_ms(started_at), "QUERY_ERROR", ex.what());
      write_json(res, 200, error);
      return;
    }

    const ExecutionOutcome execution = executor.execute(snapshot.prepared, query, options.timeout_ms);
    if (!execution.ok) {
      const std::string code = execution.timed_out ? "TIMEOUT" : "QUERY_ERROR";
      const json error = build_error(elapsed_ms(started_at), code, execution.error_message);
      write_json(res, execution.timed_out ? 408 : 200, error);
      return;
    }

    QueryResponse payload = map_result(execution.result, options.max_rows);
    payload.elapsed_ms = elapsed_ms(started_at);

    write_json(res,
               200,
               {
                   {"elapsed_ms", payload.elapsed_ms},
                   {"columns", payload.columns},
                   {"rows", payload.rows},
                   {"truncated", payload.truncated},
                   {"error", nullptr},
               });
  });

  std::cout << "[xsql-agent] Listening on http://" << kBindHost << ":" << kBindPort << "\n";
  std::cout << "[xsql-agent] Health check: http://" << kBindHost << ":" << kBindPort << "/health\n";

  if (!server.listen(kBindHost, kBindPort)) {
    std::cerr << "[xsql-agent] Failed to bind to " << kBindHost << ':' << kBindPort << "\n";
    return 1;
  }

  return 0;
}
