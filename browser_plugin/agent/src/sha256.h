#pragma once

#include <string>

namespace markql::agent::sha256 {

std::string digest_hex(const std::string& input);

}  // namespace markql::agent::sha256
