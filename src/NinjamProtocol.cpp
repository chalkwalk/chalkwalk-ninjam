#include <chalkwalk/ninjam/NinjamProtocol.h>

#include <chalkwalk/ninjam/Sha1.h>

#include <cstring>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include <chalkwalk/ninjam/Bytes.h>

namespace chalkwalk::ninjam::protocol {

// ---------------------------------------------------------------------------
// Framing
// ---------------------------------------------------------------------------

void writeFrameHeader(std::uint8_t out[kHeaderSize], std::uint8_t type,
                      std::uint32_t length) {
  out[0] = type;
  const std::uint32_t le = littleEndian(length);
  memcpy(out + 1, &le, 4);
}

bool readFrameHeader(const void *fiveBytes, FrameHeader &out) {
  const auto *b = static_cast<const std::uint8_t *>(fiveBytes);
  std::uint32_t le;
  memcpy(&le, b + 1, 4);
  const std::uint32_t len = littleEndian(le);
  if (len > kMaxPayload)
    return false;
  out.type = b[0];
  out.length = len;
  return true;
}

// ---------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------

Reader::Reader(const void *data, size_t size) noexcept
    : p(static_cast<const std::uint8_t *>(data)) {
  if (p == nullptr)
    size = 0;
  end = p + size;
}

bool Reader::need(size_t n) noexcept {
  if (failed || remaining() < n) {
    failed = true;
    return false;
  }
  return true;
}

bool Reader::u8(std::uint8_t &out) noexcept {
  if (!need(1))
    return false;
  out = *p++;
  return true;
}

bool Reader::i8(std::int8_t &out) noexcept {
  std::uint8_t v;
  if (!u8(v))
    return false;
  out = static_cast<std::int8_t>(v);
  return true;
}

bool Reader::u16le(std::uint16_t &out) noexcept {
  if (!need(2))
    return false;
  out = (std::uint16_t)((std::uint16_t)p[0] | ((std::uint16_t)p[1] << 8));
  p += 2;
  return true;
}

bool Reader::i16le(std::int16_t &out) noexcept {
  std::uint16_t v;
  if (!u16le(v))
    return false;
  // Explicit two's-complement conversion: casting an out-of-range unsigned to
  // a signed type is implementation-defined before C++20.
  out = (v & 0x8000u) ? (std::int16_t)((int)v - 65536) : (std::int16_t)v;
  return true;
}

bool Reader::u32le(std::uint32_t &out) noexcept {
  if (!need(4))
    return false;
  out = (std::uint32_t)p[0] | ((std::uint32_t)p[1] << 8) |
        ((std::uint32_t)p[2] << 16) | ((std::uint32_t)p[3] << 24);
  p += 4;
  return true;
}

bool Reader::bytes(void *dest, size_t n) noexcept {
  if (!need(n))
    return false;
  memcpy(dest, p, n);
  p += n;
  return true;
}

bool Reader::skip(size_t n) noexcept {
  if (!need(n))
    return false;
  p += n;
  return true;
}

bool Reader::cstr(std::string &out) noexcept {
  if (failed)
    return false;
  const std::uint8_t *nul = p;
  while (nul < end && *nul != 0)
    ++nul;
  if (nul >= end) {
    // No terminator before the end of the payload.
    failed = true;
    return false;
  }
  out =
      std::string(reinterpret_cast<const char *>(reinterpret_cast<const char *>(p)), (int)(nul - p));
  p = nul + 1;
  return true;
}

// ---------------------------------------------------------------------------
// Parsers
// ---------------------------------------------------------------------------

std::string guidToHex(const std::uint8_t guid[16]) {
  // Lower-case, zero-padded, no separators -- the form the server sends back
  // and the form the auth hash is computed over, so it is a wire format rather
  // than a display choice.
  static constexpr char kHex[] = "0123456789abcdef";
  std::string s;
  s.reserve(32);
  for (int i = 0; i < 16; ++i) {
    s.push_back(kHex[(guid[i] >> 4) & 0x0F]);
    s.push_back(kHex[guid[i] & 0x0F]);
  }
  return s;
}

bool IntervalBegin::isOggAudio() const {
  return fourcc[0] == 'O' && fourcc[1] == 'G' && fourcc[2] == 'G' &&
         fourcc[3] == 'v';
}

bool parseAuthChallenge(const ByteBuffer &payload, AuthChallenge &out) {
  Reader r(payload.data(), payload.size());
  return r.bytes(out.challenge, 8);
}

bool parseAuthReply(const ByteBuffer &payload, AuthReply &out) {
  out = AuthReply{};
  Reader r(payload.data(), payload.size());
  std::uint8_t flag;
  if (!r.u8(flag))
    return false;
  out.granted = (flag == 1);

  // The message and channel cap are optional trailing fields; older servers
  // send the flag alone (mpb.cpp mpb_server_auth_reply::parse).
  if (r.atEnd())
    return true;
  if (!r.cstr(out.errorMessage))
    return true; // tolerate a truncated tail rather than dropping the reply

  std::uint8_t maxchan;
  if (r.u8(maxchan))
    out.maxChannels = maxchan;
  return true;
}

ByteBuffer buildAuthReply(bool granted, const std::string &errorMessage,
                                 int maxChannels) {
  ByteBuffer b;
  const std::uint8_t flag = granted ? 1 : 0;
  appendBytes(b, &flag, 1);
  appendBytes(b, errorMessage.c_str(),
           (size_t)errorMessage.size() + 1);
  const std::uint8_t mc = (std::uint8_t)std::clamp(0, 255, maxChannels);
  appendBytes(b, &mc, 1);
  return b;
}

bool parseServerConfig(const ByteBuffer &payload, ServerConfig &out) {
  Reader r(payload.data(), payload.size());
  std::uint16_t bpm, bpi;
  if (!r.u16le(bpm) || !r.u16le(bpi))
    return false;
  out.bpm = bpm;
  out.bpi = bpi;
  return true;
}

bool parseUserInfo(const ByteBuffer &payload,
                   std::vector<UserInfoEntry> &out) {
  Reader r(payload.data(), payload.size());
  while (!r.atEnd()) {
    UserInfoEntry e;
    std::uint8_t active, chIdx;
    std::int16_t volume;
    std::int8_t pan;
    // The fixed part of a record is six bytes, not four.
    if (!r.u8(active) || !r.u8(chIdx) || !r.i16le(volume) || !r.i8(pan) ||
        !r.u8(e.flags))
      return false;
    if (!r.cstr(e.username) || !r.cstr(e.channelName))
      return false;

    e.active = (active != 0);
    e.channelIndex = chIdx;
    e.volume = volume;
    e.pan = pan;
    out.push_back(std::move(e));
  }
  return true;
}

bool parseIntervalBegin(const ByteBuffer &payload, IntervalBegin &out) {
  out = IntervalBegin{}; // never leave stale fields when reusing the struct
  Reader r(payload.data(), payload.size());
  if (!r.bytes(out.guid, 16) || !r.u32le(out.estimatedSize) ||
      !r.bytes(out.fourcc, 4))
    return false;

  std::uint8_t chIdx;
  if (!r.u8(chIdx))
    return false;
  out.channelIndex = chIdx;
  out.guidHex = guidToHex(out.guid);

  // The 0x83 upload form stops here; the 0x04 download form adds a username.
  if (!r.atEnd() && !r.cstr(out.username))
    return false;
  return true;
}

bool parseIntervalWrite(const ByteBuffer &payload, IntervalWrite &out) {
  out = IntervalWrite{};
  Reader r(payload.data(), payload.size());
  std::uint8_t flags;
  if (!r.bytes(out.guid, 16) || !r.u8(flags))
    return false;
  out.guidHex = guidToHex(out.guid);
  out.isFinal = (flags & 1) != 0;
  out.audioSize = (int)r.remaining();
  out.audioData = out.audioSize > 0 ? r.rest() : nullptr;
  return true;
}

bool parseChat(const ByteBuffer &payload, Chat &out) {
  out = Chat{}; // trailing fields are optional, so they must start empty
  Reader r(payload.data(), payload.size());
  if (!r.cstr(out.type))
    return false;

  // Trailing fields are optional: a sender may simply stop early. Only a
  // present-but-unterminated field is an error.
  std::string *fields[4] = {&out.p1, &out.p2, &out.p3, &out.p4};
  for (auto *f : fields) {
    if (r.atEnd())
      break;
    if (!r.cstr(*f))
      return false;
  }
  return true;
}

bool parseAuthUser(const ByteBuffer &payload, AuthUser &out) {
  out = AuthUser{};
  Reader r(payload.data(), payload.size());
  if (!r.bytes(out.hash, 20) || !r.cstr(out.username))
    return false;

  // Older clients stop after the username. Treat the tail as optional rather
  // than rejecting them outright.
  if (r.atEnd())
    return true;
  if (!r.u32le(out.caps))
    return false;
  if (r.atEnd())
    return true;
  return r.u32le(out.version);
}

bool parseUsermask(const ByteBuffer &payload,
                   std::vector<UsermaskEntry> &out) {
  Reader r(payload.data(), payload.size());
  while (!r.atEnd()) {
    UsermaskEntry e;
    if (!r.cstr(e.username) || !r.u32le(e.mask))
      return false;
    out.push_back(std::move(e));
  }
  return true;
}

bool parseChannelInfo(const ByteBuffer &payload,
                      std::vector<ChannelInfoEntry> &out) {
  Reader r(payload.data(), payload.size());
  std::uint16_t mpisize;
  if (!r.u16le(mpisize))
    return false;

  while (!r.atEnd()) {
    ChannelInfoEntry e;
    if (!r.cstr(e.name))
      return false;

    // The metadata block is mpisize bytes wide, of which we understand the
    // first four. Anything beyond that is skipped, not guessed at.
    std::int16_t volume = 0;
    std::int8_t pan = 0;
    if (mpisize >= 4) {
      if (!r.i16le(volume) || !r.i8(pan) || !r.u8(e.flags))
        return false;
      if (mpisize > 4 && !r.skip((size_t)(mpisize - 4)))
        return false;
    } else if (mpisize > 0 && !r.skip(mpisize)) {
      return false;
    }

    e.volume = volume;
    e.pan = pan;
    out.push_back(std::move(e));
  }
  return true;
}

// ---------------------------------------------------------------------------
// Builders
// ---------------------------------------------------------------------------

void computeAuthHash(const std::string &username, const std::string &password,
                     const std::uint8_t challenge[8], std::uint8_t out[20]) {
  Sha1 inner;
  inner.add(username.c_str(), username.size());
  inner.add(":", 1);
  inner.add(password.c_str(), password.size());
  std::uint8_t innerDigest[20];
  inner.result(innerDigest);

  Sha1 outer;
  outer.add(innerDigest, 20);
  outer.add(challenge, 8);
  outer.result(out);
}

ByteBuffer buildAuthChallenge(const std::uint8_t challenge[8],
                                     std::uint32_t caps, std::uint32_t version,
                                     const std::string &licence) {
  ByteBuffer b;
  appendBytes(b, challenge, 8);
  const std::uint32_t leCaps = littleEndian(caps);
  appendBytes(b, &leCaps, 4);
  const std::uint32_t leVer = littleEndian(version);
  appendBytes(b, &leVer, 4);
  appendBytes(b, licence.c_str(), (size_t)licence.size() + 1);
  return b;
}

ByteBuffer buildServerConfig(int bpm, int bpi) {
  ByteBuffer b;
  const std::uint16_t leBpm =
      littleEndian((std::uint16_t)bpm);
  const std::uint16_t leBpi =
      littleEndian((std::uint16_t)bpi);
  appendBytes(b, &leBpm, 2);
  appendBytes(b, &leBpi, 2);
  return b;
}

ByteBuffer buildUserInfo(const std::vector<UserInfoEntry> &entries) {
  ByteBuffer b;
  for (const auto &e : entries) {
    const std::uint8_t active = e.active ? 1 : 0;
    const std::uint8_t chIdx = (std::uint8_t)std::clamp(0, 255, e.channelIndex);
    appendBytes(b, &active, 1);
    appendBytes(b, &chIdx, 1);
    const std::uint16_t leVol =
        littleEndian((std::uint16_t)(std::int16_t)e.volume);
    appendBytes(b, &leVol, 2);
    const std::int8_t pan = (std::int8_t)std::clamp(-128, 127, e.pan);
    appendBytes(b, &pan, 1);
    appendBytes(b, &e.flags, 1);
    appendBytes(b, e.username.c_str(),
             (size_t)e.username.size() + 1);
    appendBytes(b, e.channelName.c_str(),
             (size_t)e.channelName.size() + 1);
  }
  return b;
}

ByteBuffer buildAuthUser(const std::uint8_t hash[20],
                                const std::string &username, std::uint32_t caps,
                                std::uint32_t version) {
  ByteBuffer b;
  appendBytes(b, hash, 20);
  appendBytes(b, username.c_str(), (size_t)username.size() + 1);
  const std::uint32_t leCaps = littleEndian(caps);
  appendBytes(b, &leCaps, 4);
  const std::uint32_t leVer = littleEndian(version);
  appendBytes(b, &leVer, 4);
  return b;
}

ByteBuffer
buildUsermask(const std::vector<std::pair<std::string, std::uint32_t>> &masks) {
  ByteBuffer b;
  for (const auto &[name, mask] : masks) {
    appendBytes(b, name.c_str(), (size_t)name.size() + 1);
    const std::uint32_t le = littleEndian(mask);
    appendBytes(b, &le, 4);
  }
  return b;
}

ByteBuffer buildChannelInfo(const std::vector<std::string> &names) {
  ByteBuffer b;
  // 2-byte LE mpisize: 4 bytes of per-channel metadata follow each name. The
  // server reads exactly this many bytes after every name, so a wrong value
  // desynchronises its parser for all subsequent channels.
  const std::uint8_t mpisize[2] = {4, 0};
  appendBytes(b, mpisize, 2);
  for (const auto &name : names) {
    appendBytes(b, name.c_str(), (size_t)name.size() + 1);
    const std::uint8_t meta[4] = {0, 0, 0, 0}; // volume LE (0 dB), pan, flags
    appendBytes(b, meta, 4);
  }
  return b;
}

ByteBuffer buildIntervalBegin(const std::uint8_t guid[16],
                                     std::uint32_t estimatedSize,
                                     const char fourcc[4], int channelIndex,
                                     const std::string &username) {
  ByteBuffer b;
  appendBytes(b, guid, 16);
  const std::uint32_t leSize = littleEndian(estimatedSize);
  appendBytes(b, &leSize, 4);
  appendBytes(b, fourcc, 4);
  const std::uint8_t chIdx = (std::uint8_t)channelIndex;
  appendBytes(b, &chIdx, 1);
  if (username.empty() == false)
    appendBytes(b, username.c_str(), (size_t)username.size() + 1);
  return b;
}

ByteBuffer buildIntervalWrite(const std::uint8_t guid[16], bool isFinal,
                                     const void *audio, int audioSize) {
  ByteBuffer b;
  appendBytes(b, guid, 16);
  const std::uint8_t flags = isFinal ? 1 : 0;
  appendBytes(b, &flags, 1);
  if (audio != nullptr && audioSize > 0)
    appendBytes(b, audio, (size_t)audioSize);
  return b;
}

ByteBuffer buildChat(const std::string &type, const std::string &p1,
                            const std::string &p2, const std::string &p3,
                            const std::string &p4) {
  ByteBuffer b;
  const std::string *fields[5] = {&type, &p1, &p2, &p3, &p4};
  for (auto *f : fields)
    appendString(b, *f);   // each field NUL-terminated, as the protocol writes them
  return b;
}

} // namespace chalkwalk::ninjam::protocol
