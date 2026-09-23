#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace markql {

// Evidence is advisory, snapshot-local, and independent of any model or transport.
struct ElementRecipe {
  std::string tag;
  std::vector<std::string> classes;
  std::optional<std::string> parent_tag;
  std::vector<std::string> parent_classes;
};

struct FieldCandidate {
  std::string id;
  std::string kind;
  ElementRecipe recipe;
  std::optional<std::string> attribute;
  std::vector<std::string> samples;
  // One-based element-child positions relative to the record; empty means self.
  std::vector<size_t> path;
};

struct RecordCandidate {
  std::string id;
  size_t count = 0;
  ElementRecipe recipe;
  std::vector<std::string> markers;
  std::vector<FieldCandidate> field_candidates;
};

struct StructuralEvidence {
  std::string schema = "markql.structural-evidence";
  unsigned schema_version = 1;
  bool truncated = false;
  std::vector<RecordCandidate> record_candidates;
};

// Uses the configured core HTML parser. Does no I/O and never invokes a model.
StructuralEvidence inspect_html(const std::string& html);

}  // namespace markql
