#pragma once

#include <cstdint>
#include <optional>

// Message layout (51 of the 64 bits; the top 13 are always zero):
//   50..31  BALLS     marks the message as ours (0xBA115)
//   30..29  type      what the message is about
//   28..23  x         map position (maps are at most 64 wide)
//   22..17  y
//   16..8   round     when the information was seen (up to 511)
//    7..0   checksum  rejects noise and enemy messages
namespace comms {

constexpr std::uint64_t BALLS = 0xBA115; // B A L L S
constexpr std::uint64_t SECRET = 0x5A;   // mixed into the checksum

constexpr int ENEMY_QUEEN = 1;
constexpr int FOOD = 2;

struct Message {
  int type = 0;
  int x = 0;
  int y = 0;
  int round = 0;
  int extra = 0; // not sent; kept so main.cpp compiles unchanged
};

// Folds every bit above the checksum into one byte, mixed with our secret.
inline std::uint64_t checksum(std::uint64_t value) {
  std::uint64_t c = SECRET;
  for (int shift = 8; shift < 64; shift += 8) {
    c ^= (value >> shift) & 0xFF;
  }
  return c & 0xFF;
}

inline std::uint64_t encode(Message const &m) {
  std::uint64_t v = BALLS << 31;
  v |= (static_cast<std::uint64_t>(m.type) & 0x3) << 29;
  v |= (static_cast<std::uint64_t>(m.x) & 0x3F) << 23;
  v |= (static_cast<std::uint64_t>(m.y) & 0x3F) << 17;
  v |= (static_cast<std::uint64_t>(m.round) & 0x1FF) << 8;
  v |= checksum(v);
  return v;
}

// Empty if the value isn't one of our messages. Checking v >> 31 against
// BALLS also checks that the top 13 bits are zero.
inline std::optional<Message> decode(std::uint64_t v) {
  if ((v >> 31) != BALLS || (v & 0xFF) != checksum(v)) {
    return std::nullopt;
  }
  Message m;
  m.type = static_cast<int>((v >> 29) & 0x3);
  m.x = static_cast<int>((v >> 23) & 0x3F);
  m.y = static_cast<int>((v >> 17) & 0x3F);
  m.round = static_cast<int>((v >> 8) & 0x1FF);
  return m;
}

} // namespace comms
