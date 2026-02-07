#pragma once

#include <string>
#include <string_view>

namespace xsql::util {

/// Converts a string to lowercase for case-insensitive comparisons.
/// MUST avoid locale-sensitive behavior to keep parsing deterministic.
/// Inputs are strings; outputs are lowercase strings with no side effects.
std::string to_lower(const std::string& s);
/// Converts a string to uppercase for keyword matching.
/// MUST avoid locale-sensitive behavior to keep parsing deterministic.
/// Inputs are strings; outputs are uppercase strings with no side effects.
std::string to_upper(const std::string& s);
/// Trims leading and trailing ASCII whitespace.
/// MUST preserve internal whitespace and MUST not modify the input.
/// Inputs are strings; outputs are trimmed strings with no side effects.
std::string trim_ws(const std::string& s);
/// Minifies HTML conservatively by compacting whitespace without changing structure.
/// MUST preserve attribute values and raw text content inside pre/code/textarea/script/style.
/// Inputs are HTML strings; outputs are minified HTML strings with no side effects.
std::string minify_html(std::string_view html);

}  // namespace xsql::util
