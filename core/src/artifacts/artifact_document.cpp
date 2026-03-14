#include "artifact_document.h"

#include <algorithm>
#include <unordered_set>
#include <utility>
#include <vector>

#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/verifier.h>

#include "document_generated.h"

namespace xsql::artifacts::detail {

namespace docnfb = xsql::artifacts::docnfb;

namespace {

std::string build_document_nodes_payload_legacy(const HtmlDocument& document) {
  BinaryWriter writer;
  ensure(document.nodes.size() <= kMaxNodeCount,
         "Artifact limit exceeded: node count is too large");
  writer.write_u64(static_cast<uint64_t>(document.nodes.size()));
  for (const auto& node : document.nodes) {
    writer.write_i64(node.id);
    writer.write_string(node.tag, "node tag");
    writer.write_string(node.text, "node text");
    writer.write_string(node.inner_html, "node inner_html");
    std::vector<std::pair<std::string, std::string>> attrs(node.attributes.begin(),
                                                           node.attributes.end());
    std::sort(attrs.begin(), attrs.end(),
              [](const auto& left, const auto& right) { return left.first < right.first; });
    ensure(attrs.size() <= kMaxAttributeCount,
           "Artifact limit exceeded: node attribute count is too large");
    writer.write_u64(static_cast<uint64_t>(attrs.size()));
    for (const auto& attr : attrs) {
      writer.write_string(attr.first, "attribute key");
      writer.write_string(attr.second, "attribute value");
    }
    writer.write_optional_i64(node.parent_id);
    writer.write_i64(node.max_depth);
    writer.write_i64(node.doc_order);
  }
  return writer.data();
}

HtmlDocument parse_document_nodes_legacy(const std::string& payload) {
  BinaryReader reader(payload);
  HtmlDocument document;
  const size_t node_count = read_bounded_count(
      reader, kMaxNodeCount, "Artifact limit exceeded: node count is too large");
  document.nodes.reserve(node_count);
  for (size_t i = 0; i < node_count; ++i) {
    HtmlNode node;
    node.id = reader.read_i64();
    node.tag = reader.read_string("node tag");
    node.text = reader.read_string("node text");
    node.inner_html = reader.read_string("node inner_html");
    const size_t attr_count = read_bounded_count(
        reader, kMaxAttributeCount, "Artifact limit exceeded: node attribute count is too large");
    for (size_t attr_index = 0; attr_index < attr_count; ++attr_index) {
      std::string key = reader.read_string("attribute key");
      std::string value = reader.read_string("attribute value");
      node.attributes[key] = value;
    }
    node.parent_id = reader.read_optional_i64();
    node.max_depth = reader.read_i64();
    node.doc_order = reader.read_i64();
    document.nodes.push_back(std::move(node));
  }
  ensure(reader.done(), "Corrupted artifact: trailing document nodes");
  return document;
}

std::string build_document_nodes_payload_flatbuffers(const HtmlDocument& document) {
  flatbuffers::FlatBufferBuilder builder;
  std::vector<flatbuffers::Offset<docnfb::Node>> node_offsets;
  node_offsets.reserve(document.nodes.size());

  for (const auto& node : document.nodes) {
    std::vector<std::pair<std::string, std::string>> attrs(node.attributes.begin(),
                                                           node.attributes.end());
    std::sort(attrs.begin(), attrs.end(),
              [](const auto& left, const auto& right) { return left.first < right.first; });
    std::vector<flatbuffers::Offset<docnfb::Attribute>> attr_offsets;
    attr_offsets.reserve(attrs.size());
    for (const auto& attr : attrs) {
      attr_offsets.push_back(
          docnfb::CreateAttribute(builder,
                                  builder.CreateString(attr.first),
                                  builder.CreateString(attr.second)));
    }

    node_offsets.push_back(docnfb::CreateNode(
        builder,
        node.id,
        builder.CreateString(node.tag),
        builder.CreateString(node.text),
        builder.CreateString(node.inner_html),
        builder.CreateVector(attr_offsets),
        node.parent_id.value_or(-1),
        node.parent_id.has_value(),
        node.max_depth,
        node.doc_order));
  }

  auto document_root = docnfb::CreateDocument(builder, builder.CreateVector(node_offsets));
  docnfb::FinishDocumentBuffer(builder, document_root);
  return std::string(reinterpret_cast<const char*>(builder.GetBufferPointer()), builder.GetSize());
}

HtmlDocument parse_document_nodes_flatbuffers(const std::string& payload) {
  flatbuffers::Verifier verifier(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
  ensure(docnfb::VerifyDocumentBuffer(verifier),
         "Corrupted artifact: DOCN FlatBuffer verification failed");
  ensure(docnfb::DocumentBufferHasIdentifier(payload.data()),
         "Corrupted artifact: invalid DOCN FlatBuffer identifier");

  const docnfb::Document* document_fb = docnfb::GetDocument(payload.data());
  ensure(document_fb != nullptr, "Corrupted artifact: DOCN FlatBuffer root missing");
  const auto* nodes_fb = document_fb->nodes();
  ensure(nodes_fb != nullptr, "Corrupted artifact: DOCN nodes vector missing");
  ensure(nodes_fb->size() <= kMaxNodeCount,
         "Artifact limit exceeded: node count is too large");

  HtmlDocument document;
  document.nodes.reserve(nodes_fb->size());
  for (flatbuffers::uoffset_t i = 0; i < nodes_fb->size(); ++i) {
    const docnfb::Node* node_fb = nodes_fb->Get(i);
    ensure(node_fb != nullptr, "Corrupted artifact: DOCN node entry missing");

    HtmlNode node;
    node.id = node_fb->id();
    node.tag = node_fb->tag() != nullptr ? node_fb->tag()->str() : "";
    node.text = node_fb->text() != nullptr ? node_fb->text()->str() : "";
    node.inner_html = node_fb->inner_html() != nullptr ? node_fb->inner_html()->str() : "";

    const auto* attrs_fb = node_fb->attributes();
    if (attrs_fb != nullptr) {
      ensure(attrs_fb->size() <= kMaxAttributeCount,
             "Artifact limit exceeded: node attribute count is too large");
      std::unordered_set<std::string> seen_keys;
      for (flatbuffers::uoffset_t attr_index = 0; attr_index < attrs_fb->size(); ++attr_index) {
        const docnfb::Attribute* attr_fb = attrs_fb->Get(attr_index);
        ensure(attr_fb != nullptr, "Corrupted artifact: DOCN attribute entry missing");
        const std::string key = attr_fb->key() != nullptr ? attr_fb->key()->str() : "";
        const std::string value = attr_fb->value() != nullptr ? attr_fb->value()->str() : "";
        ensure(seen_keys.insert(key).second,
               "Corrupted artifact: duplicate attribute key");
        node.attributes.emplace(key, value);
      }
    }

    if (node_fb->has_parent()) node.parent_id = node_fb->parent_id();
    node.max_depth = node_fb->max_depth();
    node.doc_order = node_fb->doc_order();
    document.nodes.push_back(std::move(node));
  }
  return document;
}

}  // namespace

uint64_t document_required_features() { return kRequiredFeatureDocnFlatbuffers; }

bool document_uses_flatbuffers(const ArtifactHeader& header) {
  return (header.required_features & kRequiredFeatureDocnFlatbuffers) != 0;
}

std::string build_document_meta_payload(const std::string& source_uri, size_t node_count) {
  BinaryWriter writer;
  writer.write_string(xsql::get_version_info().version, "producer version");
  writer.write_string(xsql::get_version_info().version, "language version");
  writer.write_string(source_uri, "source uri");
  writer.write_u64(static_cast<uint64_t>(node_count));
  return writer.data();
}

void parse_document_meta(const std::string& payload, ArtifactInfo& info) {
  BinaryReader reader(payload);
  info.producer_version = reader.read_string("producer version");
  info.language_version = reader.read_string("language version");
  info.source_uri = reader.read_string("source uri");
  info.node_count = static_cast<size_t>(reader.read_u64());
  ensure(info.node_count <= kMaxNodeCount, "Artifact limit exceeded: node count is too large");
  info.metadata_available = true;
  ensure(reader.done(), "Corrupted artifact: trailing document metadata");
}

std::string build_document_nodes_payload(const HtmlDocument& document) {
  validate_document(document);
  return build_document_nodes_payload_flatbuffers(document);
}

HtmlDocument parse_document_nodes(const ArtifactHeader& header, const std::string& payload) {
  HtmlDocument document = document_uses_flatbuffers(header) ? parse_document_nodes_flatbuffers(payload)
                                                            : parse_document_nodes_legacy(payload);
  validate_document(document);
  return document;
}

void validate_document(const HtmlDocument& document) {
  const size_t node_count = document.nodes.size();
  ensure(node_count <= kMaxNodeCount, "Artifact limit exceeded: node count is too large");
  std::vector<bool> seen(node_count, false);
  for (const auto& node : document.nodes) {
    ensure(node.id >= 0 && static_cast<size_t>(node.id) < node_count,
           "Corrupted artifact: node_id is out of range");
    ensure(!seen[static_cast<size_t>(node.id)],
           "Corrupted artifact: duplicate node_id");
    seen[static_cast<size_t>(node.id)] = true;
    ensure(node.attributes.size() <= kMaxAttributeCount,
           "Artifact limit exceeded: node attribute count is too large");
    validate_utf8_text(node.tag, "node tag");
    validate_utf8_text(node.text, "node text");
    validate_utf8_text(node.inner_html, "node inner_html");
    ensure(node.max_depth >= 0, "Corrupted artifact: max_depth is negative");
    ensure(node.doc_order >= 0, "Corrupted artifact: doc_order is negative");
    if (node.parent_id.has_value()) {
      ensure(*node.parent_id >= 0 && static_cast<size_t>(*node.parent_id) < node_count,
             "Corrupted artifact: parent_id is out of range");
      ensure(*node.parent_id != node.id,
             "Corrupted artifact: parent_id cannot equal node_id");
    }
    for (const auto& attr : node.attributes) {
      validate_utf8_text(attr.first, "attribute key");
      validate_utf8_text(attr.second, "attribute value");
    }
  }
}

}  // namespace xsql::artifacts::detail
