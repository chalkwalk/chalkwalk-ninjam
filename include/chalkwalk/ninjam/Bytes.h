// SPDX-License-Identifier: MIT
// Part of chalkwalk-ninjam. See LICENSE.
#pragma once

// Bytes on the wire.
//
// NINJAM's protocol is little-endian, length-prefixed and byte-exact, so the
// two things this file provides are the two things every message needs: a
// growable byte buffer, and a way to write an integer without caring what the
// host's endianness is.

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace chalkwalk::ninjam {

// A payload. `std::vector<std::uint8_t>` rather than a bespoke type: callers
// already know how to use it, it owns its memory, and it costs the library no
// vocabulary of its own.
using ByteBuffer = std::vector<std::uint8_t>;

inline void appendBytes(ByteBuffer &out, const void *data, std::size_t n) {
  const auto *p = static_cast<const std::uint8_t *>(data);
  out.insert(out.end(), p, p + n);
}

inline void appendBytes(ByteBuffer &out, const std::string &s) {
  appendBytes(out, s.data(), s.size());
}

// A NUL-terminated string, which is how NINJAM writes every one of them.
inline void appendString(ByteBuffer &out, const std::string &s) {
  appendBytes(out, s.data(), s.size());
  out.push_back(0);
}

// Little-endian conversion, both directions -- the operation is its own
// inverse, which is why one name serves for reading and writing.
//
// A no-op on a little-endian host, which is every machine this will realistically
// run on, and arithmetic over bytes on any other -- so it is correct on a
// big-endian host without one ever being available to test on.
[[nodiscard]] inline std::uint16_t littleEndian(std::uint16_t v) noexcept {
  if constexpr (std::endian::native == std::endian::little)
    return v;
  else
    return static_cast<std::uint16_t>((v >> 8) | (v << 8));
}

[[nodiscard]] inline std::uint32_t littleEndian(std::uint32_t v) noexcept {
  if constexpr (std::endian::native == std::endian::little)
    return v;
  else
    return ((v >> 24) & 0x000000FFu) | ((v >> 8) & 0x0000FF00u) |
           ((v << 8) & 0x00FF0000u) | ((v << 24) & 0xFF000000u);
}

}  // namespace chalkwalk::ninjam
