#include "artifact_internal.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>
#include <unordered_set>

namespace xsql::artifacts::detail {

namespace {

bool is_valid_utf8(const std::string& text) {
  size_t i = 0;
  while (i < text.size()) {
    unsigned char lead = static_cast<unsigned char>(text[i]);
    if ((lead & 0x80) == 0) {
      ++i;
      continue;
    }

    size_t len = 0;
    uint32_t cp = 0;
    uint32_t min_cp = 0;
    if ((lead & 0xE0) == 0xC0) {
      len = 2;
      cp = lead & 0x1Fu;
      min_cp = 0x80;
    } else if ((lead & 0xF0) == 0xE0) {
      len = 3;
      cp = lead & 0x0Fu;
      min_cp = 0x800;
    } else if ((lead & 0xF8) == 0xF0) {
      len = 4;
      cp = lead & 0x07u;
      min_cp = 0x10000;
    } else {
      return false;
    }
    if (i + len > text.size()) return false;
    for (size_t j = 1; j < len; ++j) {
      unsigned char c = static_cast<unsigned char>(text[i + j]);
      if ((c & 0xC0) != 0x80) return false;
      cp = (cp << 6) | (c & 0x3Fu);
    }
    if (cp < min_cp) return false;
    if (cp > 0x10FFFF) return false;
    if (cp >= 0xD800 && cp <= 0xDFFF) return false;
    i += len;
  }
  return true;
}

}  // namespace

void ensure(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

uint32_t current_producer_major() {
  const std::string version = xsql::get_version_info().version;
  size_t end = version.find('.');
  std::string major_text = end == std::string::npos ? version : version.substr(0, end);
  try {
    return static_cast<uint32_t>(std::stoul(major_text));
  } catch (const std::exception&) {
    return 0;
  }
}

uint32_t current_language_major() {
  return current_producer_major();
}

void validate_utf8_text(const std::string& value, const std::string& field_name) {
  ensure(value.size() <= kMaxStringBytes,
         "Artifact limit exceeded: " + field_name + " is too large");
  ensure(is_valid_utf8(value),
         "Corrupted artifact: " + field_name + " is not valid UTF-8");
}

uint64_t fnv1a64(const std::string& data) {
  uint64_t hash = 14695981039346656037ull;
  for (unsigned char byte : data) {
    hash ^= static_cast<uint64_t>(byte);
    hash *= 1099511628211ull;
  }
  return hash;
}

size_t checked_next_depth(size_t depth, size_t max_depth, const std::string& message) {
  ensure(depth < max_depth, message);
  return depth + 1;
}

ArtifactKind kind_from_magic(const std::string& magic) {
  if (magic == std::string(kMagicDocument, sizeof(kMagicDocument))) {
    return ArtifactKind::DocumentSnapshot;
  }
  if (magic == std::string(kMagicPrepared, sizeof(kMagicPrepared))) {
    return ArtifactKind::PreparedQuery;
  }
  return ArtifactKind::None;
}

const char* magic_for_kind(ArtifactKind kind) {
  if (kind == ArtifactKind::DocumentSnapshot) return kMagicDocument;
  if (kind == ArtifactKind::PreparedQuery) return kMagicPrepared;
  throw std::runtime_error("Unsupported artifact kind");
}

std::string read_file_bytes(const std::string& path) {
  std::ifstream in(path, std::ios::binary | std::ios::ate);
  if (!in) throw std::runtime_error("Failed to open artifact file: " + path);
  std::ifstream::pos_type end = in.tellg();
  ensure(end >= 0, "Failed to read artifact file: " + path);
  const uint64_t size = static_cast<uint64_t>(end);
  ensure(size <= kMaxArtifactBytes, "Artifact limit exceeded: file is too large");
  std::string data(static_cast<size_t>(size), '\0');
  in.seekg(0, std::ios::beg);
  if (!data.empty()) in.read(data.data(), static_cast<std::streamsize>(data.size()));
  if (!in && !data.empty()) throw std::runtime_error("Failed to read artifact file: " + path);
  return data;
}

void write_file_bytes(const std::string& path, const std::string& data) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) throw std::runtime_error("Failed to create artifact file: " + path);
  out.write(data.data(), static_cast<std::streamsize>(data.size()));
  if (!out) throw std::runtime_error("Failed to write artifact file: " + path);
}

size_t read_bounded_count(BinaryReader& reader,
                          size_t max_count,
                          const std::string& message) {
  const uint64_t count = reader.read_u64();
  ensure(count <= static_cast<uint64_t>(max_count), message);
  ensure(count <= static_cast<uint64_t>(std::numeric_limits<size_t>::max()),
         "Corrupted artifact: count overflow");
  return static_cast<size_t>(count);
}

bool is_valid_section_tag(const std::string& tag) {
  if (tag.size() != 4) return false;
  for (unsigned char ch : tag) {
    if (ch < 0x21 || ch > 0x7Eu) return false;
  }
  return true;
}

std::string build_artifact_bytes(ArtifactKind kind,
                                 uint32_t producer_major,
                                 uint64_t required_features,
                                 const std::vector<SectionView>& sections) {
  ensure(sections.size() <= kMaxSectionCount,
         "Artifact limit exceeded: too many sections");
  BinaryWriter payload_writer;
  std::unordered_set<std::string> seen_tags;
  for (const auto& section : sections) {
    ensure(is_valid_section_tag(section.tag), "Invalid artifact section tag");
    ensure(section.payload.size() <= kMaxSectionBytes,
           "Artifact limit exceeded: section payload is too large");
    ensure(seen_tags.insert(section.tag).second,
           "Invalid artifact: duplicate section tag");
    payload_writer.write_bytes(section.tag.data(), section.tag.size());
    payload_writer.write_u64(static_cast<uint64_t>(section.payload.size()));
    payload_writer.write_bytes(section.payload.data(), section.payload.size());
  }
  ensure(payload_writer.data().size() <= kMaxArtifactBytes - kHeaderBytes,
         "Artifact limit exceeded: payload is too large");

  BinaryWriter out;
  out.write_bytes(magic_for_kind(kind), 4);
  out.write_u16(kArtifactFormatMajor);
  out.write_u16(kArtifactFormatMinor);
  out.write_u32(kHeaderBytes);
  out.write_u32(static_cast<uint32_t>(sections.size()));
  out.write_u64(required_features);
  out.write_u64(static_cast<uint64_t>(payload_writer.data().size()));
  out.write_u64(fnv1a64(payload_writer.data()));
  out.write_u32(producer_major);
  out.write_u32(current_language_major());
  out.write_u32(0);
  out.write_u32(0);
  out.write_u32(0);
  out.write_u32(0);
  out.write_bytes(payload_writer.data().data(), payload_writer.data().size());
  return out.data();
}

ArtifactHeader read_header(BinaryReader& reader) {
  ArtifactHeader header;
  header.kind = kind_from_magic(reader.read_bytes(4));
  ensure(header.kind != ArtifactKind::None,
         "Unsupported artifact: unknown magic header");
  header.format_major = reader.read_u16();
  header.format_minor = reader.read_u16();
  uint32_t header_bytes = reader.read_u32();
  ensure(header_bytes == kHeaderBytes,
         "Unsupported artifact: unexpected header size");
  header.section_count = reader.read_u32();
  ensure(header.section_count > 0 && header.section_count <= kMaxSectionCount,
         "Artifact limit exceeded: invalid section count");
  header.required_features = reader.read_u64();
  header.payload_bytes = reader.read_u64();
  ensure(header.payload_bytes <= kMaxArtifactBytes - kHeaderBytes,
         "Artifact limit exceeded: payload is too large");
  header.payload_checksum = reader.read_u64();
  header.producer_major = reader.read_u32();
  header.language_major = reader.read_u32();
  (void)reader.read_u32();
  (void)reader.read_u32();
  (void)reader.read_u32();
  (void)reader.read_u32();
  return header;
}

std::vector<SectionView> read_sections(BinaryReader& reader, const ArtifactHeader& header) {
  const uint64_t payload_bytes = header.payload_bytes;
  ensure(payload_bytes <= static_cast<uint64_t>(std::numeric_limits<size_t>::max()),
         "Corrupted artifact: payload length overflow");
  ensure(reader.has_remaining(static_cast<size_t>(payload_bytes)),
         "Corrupted artifact: invalid payload length");
  const size_t payload_begin = reader.position();
  const size_t payload_end = payload_begin + static_cast<size_t>(payload_bytes);
  std::string payload_copy = reader.read_bytes(static_cast<size_t>(payload_bytes));
  ensure(fnv1a64(payload_copy) == header.payload_checksum,
         "Corrupted artifact: checksum mismatch");
  BinaryReader payload_reader(payload_copy);
  std::vector<SectionView> sections;
  sections.reserve(header.section_count);
  std::unordered_set<std::string> seen_tags;
  while (payload_reader.position() < payload_end - payload_begin) {
    ensure(payload_reader.has_remaining(kSectionHeaderBytes),
           "Corrupted artifact: incomplete section header");
    const std::string tag = payload_reader.read_bytes(4);
    ensure(is_valid_section_tag(tag), "Corrupted artifact: invalid section tag");
    ensure(seen_tags.insert(tag).second,
           "Corrupted artifact: duplicate section tag");
    const uint64_t section_size = payload_reader.read_u64();
    ensure(section_size <= kMaxSectionBytes,
           "Artifact limit exceeded: section payload is too large");
    ensure(section_size <= static_cast<uint64_t>(std::numeric_limits<size_t>::max()),
           "Corrupted artifact: section length overflow");
    ensure(payload_reader.has_remaining(static_cast<size_t>(section_size)),
           "Corrupted artifact: truncated section payload");
    sections.push_back(
        SectionView{tag, payload_reader.read_bytes(static_cast<size_t>(section_size))});
  }
  ensure(payload_reader.done(), "Corrupted artifact: payload alignment mismatch");
  ensure(sections.size() == header.section_count,
         "Corrupted artifact: section count mismatch");
  ensure(reader.position() == payload_end, "Corrupted artifact: payload alignment mismatch");
  ensure(reader.done(), "Corrupted artifact: trailing bytes after payload");
  return sections;
}

const SectionView* find_section(const std::vector<SectionView>& sections, const std::string& tag) {
  for (const auto& section : sections) {
    if (section.tag == tag) return &section;
  }
  return nullptr;
}

std::string compatibility_error_for_header(const ArtifactHeader& header) {
  if (header.format_major != kArtifactFormatMajor) {
    return "Unsupported artifact: format major version mismatch";
  }
  if ((header.required_features & ~kKnownRequiredFeatures) != 0) {
    return "Unsupported artifact: unknown required feature flags";
  }
  if (header.producer_major != current_producer_major()) {
    return "Unsupported artifact: producer major version mismatch";
  }
  if (header.language_major != current_language_major()) {
    return "Unsupported artifact: language major version mismatch";
  }
  return "";
}

}  // namespace xsql::artifacts::detail
