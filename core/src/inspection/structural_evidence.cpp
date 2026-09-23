#include "markql/inspection.h"

#include <algorithm>
#include <map>
#include <sstream>
#include <tuple>

#include "dom/html_parser.h"

namespace markql {
namespace {

using Index = size_t;
using Children = std::vector<std::vector<Index>>;

struct FieldKey {
  std::vector<size_t> path;
  std::string tag;
  std::optional<std::string> attribute;

  bool operator<(const FieldKey& other) const {
    return std::tie(path, tag, attribute) < std::tie(other.path, other.tag, other.attribute);
  }
};

using Fields = std::map<FieldKey, FieldCandidate>;

constexpr size_t kMaxRecords = 64;
constexpr size_t kMaxFields = 64;
constexpr size_t kMaxSampleBytes = 256;
constexpr size_t kMaxVisits = 100000;
constexpr size_t kMaxDepth = 32;

std::string sample_prefix(const std::string& value, bool& truncated) {
  if (value.size() <= kMaxSampleBytes) return value;
  truncated = true;
  size_t end = kMaxSampleBytes;
  // Do not split a UTF-8 code point.
  while (end > 0 && (static_cast<unsigned char>(value[end]) & 0xc0) == 0x80) --end;
  return value.substr(0, end);
}

std::vector<std::string> classes(const HtmlNode& node) {
  std::vector<std::string> result;
  auto found = node.attributes.find("class");
  if (found == node.attributes.end()) return result;
  std::istringstream stream(found->second);
  for (std::string token; stream >> token;) result.push_back(token);
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

ElementRecipe recipe(const HtmlDocument& doc, Index index) {
  const auto& node = doc.nodes[index];
  ElementRecipe result;
  result.tag = node.tag;
  result.classes = classes(node);
  if (node.parent_id) {
    const auto& parent = doc.nodes.at(static_cast<Index>(*node.parent_id));
    result.parent_tag = parent.tag;
    result.parent_classes = classes(parent);
  }
  return result;
}

void retain_common(std::vector<std::string>& values, const std::vector<std::string>& other) {
  values.erase(std::remove_if(values.begin(), values.end(), [&](const auto& value) {
    return !std::binary_search(other.begin(), other.end(), value);
  }), values.end());
}

void merge_recipe(ElementRecipe& target, const ElementRecipe& other) {
  retain_common(target.classes, other.classes);
  if (target.parent_tag != other.parent_tag) {
    target.parent_tag.reset();
    target.parent_classes.clear();
  } else {
    retain_common(target.parent_classes, other.parent_classes);
  }
}

void add_value(Fields& fields, const FieldKey& key, const ElementRecipe& source,
               const std::string& value, bool& truncated) {
  if (fields.size() >= kMaxFields && fields.find(key) == fields.end()) {
    truncated = true;
    return;
  }
  const auto sample = sample_prefix(value, truncated);
  auto [entry, inserted] = fields.try_emplace(key);
  auto& field = entry->second;
  if (inserted) {
    field.kind = key.attribute ? "attribute" : "text";
    field.recipe = source;
    field.attribute = key.attribute;
    field.path = key.path;
  } else {
    merge_recipe(field.recipe, source);
  }
  if (field.samples.size() < 3 &&
      std::find(field.samples.begin(), field.samples.end(), sample) == field.samples.end()) {
    field.samples.push_back(sample);
  }
}

StructuralEvidence analyze(const HtmlDocument& doc) {
  Children children(doc.nodes.size() + 1);
  for (Index i = 0; i < doc.nodes.size(); ++i) {
    const auto parent = doc.nodes[i].parent_id;
    children[parent ? static_cast<Index>(*parent) : doc.nodes.size()].push_back(i);
  }
  StructuralEvidence result;
  size_t visits = 0;
  for (const auto& siblings : children) {
    std::map<std::string, std::vector<Index>> groups;
    for (Index index : siblings) {
      const auto& node = doc.nodes[index];
      if (children[index].empty()) continue;
      std::string shape = node.tag;
      for (Index child : children[index]) shape += "/" + doc.nodes[child].tag;
      groups[shape].push_back(index);
    }
    for (const auto& entry : groups) {
      const auto& members = entry.second;
      if (members.size() < 3) continue;
      if (result.record_candidates.size() == kMaxRecords || visits >= kMaxVisits) {
        result.truncated = true;
        return result;
      }
      RecordCandidate record;
      record.id = "R" + std::to_string(result.record_candidates.size() + 1);
      record.count = members.size();
      record.recipe = recipe(doc, members.front());
      // Group value suppliers by their structural path, never by their text.
      Fields fields;
      for (Index member : members) {
        merge_recipe(record.recipe, recipe(doc, member));
        if (visits >= kMaxVisits) {
          result.truncated = true;
          continue;
        }
        std::vector<std::pair<Index, std::vector<size_t>>> pending{{member, {}}};
        while (!pending.empty()) {
          if (visits++ >= kMaxVisits) {
            result.truncated = true;
            break;
          }
          auto [index, path] = std::move(pending.back());
          pending.pop_back();
          const auto& node = doc.nodes[index];
          if (node.tag == "script" || node.tag == "style" || node.tag == "template") continue;
          if (!node.text.empty()) {
            add_value(fields, {path, node.tag, std::nullopt}, recipe(doc, index), node.text,
                      result.truncated);
          }
          for (const auto* attribute : {"href", "src", "title", "id", "style", "aria-label"}) {
            auto found = node.attributes.find(attribute);
            if (found != node.attributes.end() && !found->second.empty()) {
              add_value(fields, {path, node.tag, attribute}, recipe(doc, index), found->second,
                        result.truncated);
            }
          }
          if (path.size() >= kMaxDepth) {
            if (!children[index].empty()) result.truncated = true;
            continue;
          }
          for (Index pos = children[index].size(); pos > 0; --pos) {
            auto child_path = path;
            child_path.push_back(pos);
            pending.emplace_back(children[index][pos - 1], std::move(child_path));
          }
        }
      }
      for (auto& entry : fields) {
        auto& field = entry.second;
        field.id = record.id + ".F" + std::to_string(record.field_candidates.size() + 1);
        record.field_candidates.push_back(std::move(field));
      }
      result.record_candidates.push_back(std::move(record));
    }
  }
  return result;
}

}  // namespace

StructuralEvidence inspect_html(const std::string& html) {
  return analyze(parse_html(html));
}

}  // namespace markql
