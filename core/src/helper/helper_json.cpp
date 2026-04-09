#include "helper_json.h"

#include <sstream>

#include "helper_policy.h"

namespace markql::helper {

namespace {

std::string escape_json(const std::string& text) {
  std::string out;
  out.reserve(text.size() + 8);
  for (char c : text) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out += c;
        break;
    }
  }
  return out;
}

void append_string_array(std::ostringstream& oss, const std::vector<std::string>& values) {
  oss << "[";
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i > 0) oss << ",";
    oss << "\"" << escape_json(values[i]) << "\"";
  }
  oss << "]";
}

}  // namespace

std::string to_json(const RetrievalPack& pack) {
  std::ostringstream oss;
  oss << "{";
  oss << "\"topic\":\"" << escape_json(to_string(pack.topic)) << "\",";
  oss << "\"summary\":\"" << escape_json(pack.summary) << "\",";
  oss << "\"facts\":";
  append_string_array(oss, pack.facts);
  oss << ",\"examples\":";
  append_string_array(oss, pack.examples);
  oss << ",\"doc_refs\":";
  append_string_array(oss, pack.doc_refs);
  oss << "}";
  return oss.str();
}

std::string to_json(const ModelDecision& decision) {
  std::ostringstream oss;
  oss << "{";
  oss << "\"status\":\"" << escape_json(to_string(decision.status)) << "\",";
  oss << "\"diagnosis\":\"" << escape_json(to_string(decision.diagnosis)) << "\",";
  oss << "\"reason\":\"" << escape_json(decision.reason) << "\",";
  oss << "\"chosen_family\":\"" << escape_json(decision.chosen_family) << "\",";
  oss << "\"requested_artifact\":\"" << escape_json(to_string(decision.requested_artifact)) << "\",";
  oss << "\"query\":\"" << escape_json(decision.query) << "\",";
  oss << "\"next_action\":\"" << escape_json(decision.next_action) << "\"";
  oss << "}";
  return oss.str();
}

std::string to_json(const FinalSuggestion& suggestion) {
  std::ostringstream oss;
  oss << "{";
  oss << "\"status\":\"" << escape_json(suggestion.status) << "\",";
  oss << "\"mode\":\"" << escape_json(to_string(suggestion.mode)) << "\",";
  oss << "\"query\":\"" << escape_json(suggestion.query) << "\",";
  oss << "\"diagnosis\":\"" << escape_json(to_string(suggestion.diagnosis)) << "\",";
  oss << "\"reason\":\"" << escape_json(suggestion.reason) << "\",";
  oss << "\"artifact_used\":\"" << escape_json(to_string(suggestion.artifact_used)) << "\",";
  oss << "\"retrieval_topic\":\"" << escape_json(to_string(suggestion.retrieval_topic)) << "\"";
  oss << "}";
  return oss.str();
}

std::string to_json(const ControllerStep& step) {
  std::ostringstream oss;
  oss << "{";
  oss << "\"state\":\"" << escape_json(to_string(step.state)) << "\",";
  oss << "\"action\":\"" << escape_json(to_string(step.action)) << "\",";
  oss << "\"requested_artifact\":\"" << escape_json(to_string(step.requested_artifact)) << "\",";
  oss << "\"requested_pack\":\"" << escape_json(to_string(step.requested_pack)) << "\",";
  oss << "\"model_task\":\"" << escape_json(to_string(step.model_task)) << "\",";
  oss << "\"diagnosis\":\"" << escape_json(to_string(step.diagnosis)) << "\",";
  oss << "\"reason\":\"" << escape_json(step.reason) << "\",";
  oss << "\"query\":\"" << escape_json(step.query) << "\"";
  oss << "}";
  return oss.str();
}

}  // namespace markql::helper
