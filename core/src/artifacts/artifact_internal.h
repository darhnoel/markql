#pragma once

#include "artifacts.h"

#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

namespace markql::artifacts::detail {

constexpr uint16_t kArtifactFormatMajor = 2;
constexpr uint16_t kArtifactFormatMinor = 1;
constexpr uint32_t kHeaderBytes = 64;
constexpr uint32_t kSectionHeaderBytes = 12;
constexpr char kMagicDocument[4] = {'M', 'Q', 'D', '\0'};
constexpr char kMagicPrepared[4] = {'M', 'Q', 'P', '\0'};
constexpr uint64_t kRequiredFeatureDocnFlatbuffers = 1ull << 0;
constexpr uint64_t kRequiredFeatureQastFlatbuffers = 1ull << 1;
constexpr uint64_t kKnownRequiredFeatures =
    kRequiredFeatureDocnFlatbuffers | kRequiredFeatureQastFlatbuffers;
constexpr uint64_t kMaxArtifactBytes = 256ull * 1024ull * 1024ull;
constexpr uint32_t kMaxSectionCount = 16;
constexpr uint64_t kMaxSectionBytes = kMaxArtifactBytes;
constexpr size_t kMaxStringBytes = 8ull * 1024ull * 1024ull;
constexpr size_t kMaxNodeCount = 1000000;
constexpr size_t kMaxAttributeCount = 4096;
constexpr size_t kMaxCollectionCount = 100000;
constexpr size_t kMaxQueryDepth = 64;
constexpr size_t kMaxExprDepth = 128;
constexpr size_t kMaxScalarExprDepth = 128;
constexpr size_t kMaxFlattenExprDepth = 64;

void ensure(bool condition, const std::string& message);
uint32_t current_producer_major();
uint32_t current_language_major();
void validate_utf8_text(const std::string& value, const std::string& field_name);
uint64_t fnv1a64(const std::string& data);
size_t checked_next_depth(size_t depth, size_t max_depth, const std::string& message);

class BinaryWriter {
 public:
  void write_u8(uint8_t value) { data_.push_back(static_cast<char>(value)); }
  void write_bool(bool value) { write_u8(value ? 1 : 0); }

  void write_u16(uint16_t value) {
    write_u8(static_cast<uint8_t>(value & 0xffu));
    write_u8(static_cast<uint8_t>((value >> 8) & 0xffu));
  }

  void write_u32(uint32_t value) {
    for (int i = 0; i < 4; ++i) {
      write_u8(static_cast<uint8_t>((value >> (i * 8)) & 0xffu));
    }
  }

  void write_u64(uint64_t value) {
    for (int i = 0; i < 8; ++i) {
      write_u8(static_cast<uint8_t>((value >> (i * 8)) & 0xffu));
    }
  }

  void write_i64(int64_t value) { write_u64(static_cast<uint64_t>(value)); }

  void write_bytes(const char* bytes, size_t size) { data_.append(bytes, size); }

  void write_string(const std::string& value,
                    const std::string& field_name = "artifact string") {
    validate_utf8_text(value, field_name);
    write_u64(static_cast<uint64_t>(value.size()));
    write_bytes(value.data(), value.size());
  }

  void write_optional_string(const std::optional<std::string>& value,
                             const std::string& field_name = "artifact string") {
    write_bool(value.has_value());
    if (value.has_value()) write_string(*value, field_name);
  }

  void write_optional_u64(const std::optional<size_t>& value) {
    write_bool(value.has_value());
    if (value.has_value()) write_u64(static_cast<uint64_t>(*value));
  }

  void write_optional_i64(const std::optional<int64_t>& value) {
    write_bool(value.has_value());
    if (value.has_value()) write_i64(*value);
  }

  const std::string& data() const { return data_; }

 private:
  std::string data_;
};

class BinaryReader {
 public:
  explicit BinaryReader(const std::string& data) : data_(data) {}

  bool has_remaining(size_t bytes) const { return bytes <= remaining(); }
  bool done() const { return pos_ == data_.size(); }
  size_t position() const { return pos_; }
  size_t remaining() const { return data_.size() - pos_; }

  uint8_t read_u8() {
    require(1);
    return static_cast<uint8_t>(data_[pos_++]);
  }

  bool read_bool() {
    uint8_t value = read_u8();
    ensure(value == 0 || value == 1, "Corrupted artifact: invalid boolean value");
    return value == 1;
  }

  uint16_t read_u16() {
    uint16_t value = 0;
    for (int i = 0; i < 2; ++i) value |= static_cast<uint16_t>(read_u8()) << (i * 8);
    return value;
  }

  uint32_t read_u32() {
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) value |= static_cast<uint32_t>(read_u8()) << (i * 8);
    return value;
  }

  uint64_t read_u64() {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) value |= static_cast<uint64_t>(read_u8()) << (i * 8);
    return value;
  }

  int64_t read_i64() { return static_cast<int64_t>(read_u64()); }

  std::string read_bytes(size_t size) {
    require(size);
    std::string out = data_.substr(pos_, size);
    pos_ += size;
    return out;
  }

  std::string read_string(const std::string& field_name = "artifact string",
                          size_t max_bytes = kMaxStringBytes) {
    uint64_t size = read_u64();
    ensure(size <= static_cast<uint64_t>(max_bytes),
           "Artifact limit exceeded: " + field_name + " is too large");
    ensure(size <= static_cast<uint64_t>(std::numeric_limits<size_t>::max()),
           "Corrupted artifact: string length overflow");
    std::string value = read_bytes(static_cast<size_t>(size));
    validate_utf8_text(value, field_name);
    return value;
  }

  std::optional<std::string> read_optional_string(
      const std::string& field_name = "artifact string",
      size_t max_bytes = kMaxStringBytes) {
    if (!read_bool()) return std::nullopt;
    return read_string(field_name, max_bytes);
  }

  std::optional<size_t> read_optional_u64() {
    if (!read_bool()) return std::nullopt;
    uint64_t value = read_u64();
    ensure(value <= static_cast<uint64_t>(std::numeric_limits<size_t>::max()),
           "Corrupted artifact: size value overflow");
    return static_cast<size_t>(value);
  }

  std::optional<int64_t> read_optional_i64() {
    if (!read_bool()) return std::nullopt;
    return read_i64();
  }

 private:
  void require(size_t size) const {
    if (!has_remaining(size)) {
      throw std::runtime_error("Corrupted artifact: unexpected end of file");
    }
  }

  const std::string& data_;
  size_t pos_ = 0;
};

struct SectionView {
  std::string tag;
  std::string payload;
};

ArtifactKind kind_from_magic(const std::string& magic);
const char* magic_for_kind(ArtifactKind kind);
std::string read_file_bytes(const std::string& path);
void write_file_bytes(const std::string& path, const std::string& data);
size_t read_bounded_count(BinaryReader& reader,
                          size_t max_count,
                          const std::string& message);
bool is_valid_section_tag(const std::string& tag);
std::string build_artifact_bytes(ArtifactKind kind,
                                 uint32_t producer_major,
                                 uint64_t required_features,
                                 const std::vector<SectionView>& sections);
ArtifactHeader read_header(BinaryReader& reader);
std::vector<SectionView> read_sections(BinaryReader& reader, const ArtifactHeader& header);
const SectionView* find_section(const std::vector<SectionView>& sections, const std::string& tag);
std::string compatibility_error_for_header(const ArtifactHeader& header);

template <typename Enum>
Enum enum_from_u8(uint8_t value, Enum max_value, const std::string& message) {
  if (value > static_cast<uint8_t>(max_value)) {
    throw std::runtime_error(message);
  }
  return static_cast<Enum>(value);
}

}  // namespace markql::artifacts::detail
