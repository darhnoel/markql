#include "sha256.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace xsql::agent::sha256 {

namespace {

constexpr std::array<uint32_t, 64> kRoundConstants = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

constexpr std::array<uint32_t, 8> kInitialState = {
    0x6a09e667,
    0xbb67ae85,
    0x3c6ef372,
    0xa54ff53a,
    0x510e527f,
    0x9b05688c,
    0x1f83d9ab,
    0x5be0cd19,
};

inline uint32_t rotr(uint32_t value, uint32_t shift) {
  return (value >> shift) | (value << (32U - shift));
}

inline uint32_t choose(uint32_t x, uint32_t y, uint32_t z) {
  return (x & y) ^ (~x & z);
}

inline uint32_t majority(uint32_t x, uint32_t y, uint32_t z) {
  return (x & y) ^ (x & z) ^ (y & z);
}

inline uint32_t big_sigma0(uint32_t x) {
  return rotr(x, 2U) ^ rotr(x, 13U) ^ rotr(x, 22U);
}

inline uint32_t big_sigma1(uint32_t x) {
  return rotr(x, 6U) ^ rotr(x, 11U) ^ rotr(x, 25U);
}

inline uint32_t small_sigma0(uint32_t x) {
  return rotr(x, 7U) ^ rotr(x, 18U) ^ (x >> 3U);
}

inline uint32_t small_sigma1(uint32_t x) {
  return rotr(x, 17U) ^ rotr(x, 19U) ^ (x >> 10U);
}

void process_block(const uint8_t* block, std::array<uint32_t, 8>& state) {
  std::array<uint32_t, 64> w{};
  for (size_t i = 0; i < 16; ++i) {
    w[i] = (static_cast<uint32_t>(block[i * 4]) << 24U) |
           (static_cast<uint32_t>(block[i * 4 + 1]) << 16U) |
           (static_cast<uint32_t>(block[i * 4 + 2]) << 8U) |
           static_cast<uint32_t>(block[i * 4 + 3]);
  }
  for (size_t i = 16; i < 64; ++i) {
    w[i] = small_sigma1(w[i - 2]) + w[i - 7] + small_sigma0(w[i - 15]) + w[i - 16];
  }

  uint32_t a = state[0];
  uint32_t b = state[1];
  uint32_t c = state[2];
  uint32_t d = state[3];
  uint32_t e = state[4];
  uint32_t f = state[5];
  uint32_t g = state[6];
  uint32_t h = state[7];

  for (size_t i = 0; i < 64; ++i) {
    const uint32_t t1 = h + big_sigma1(e) + choose(e, f, g) + kRoundConstants[i] + w[i];
    const uint32_t t2 = big_sigma0(a) + majority(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }

  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  state[5] += f;
  state[6] += g;
  state[7] += h;
}

std::string encode_hex(const std::array<uint32_t, 8>& state) {
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (uint32_t value : state) {
    out << std::setw(8) << value;
  }
  return out.str();
}

}  // namespace

std::string digest_hex(const std::string& input) {
  std::array<uint32_t, 8> state = kInitialState;

  const uint64_t bit_length = static_cast<uint64_t>(input.size()) * 8ULL;
  std::string data = input;
  data.push_back(static_cast<char>(0x80));

  while ((data.size() % 64) != 56) {
    data.push_back(static_cast<char>(0x00));
  }

  for (int i = 7; i >= 0; --i) {
    data.push_back(static_cast<char>((bit_length >> (i * 8)) & 0xffULL));
  }

  for (size_t offset = 0; offset < data.size(); offset += 64) {
    process_block(reinterpret_cast<const uint8_t*>(data.data() + offset), state);
  }

  return encode_hex(state);
}

}  // namespace xsql::agent::sha256
