#pragma once

#include <chalkwalk/ninjam/Bytes.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

// Byte-level Ninjam protocol: framing, message parsing, message building.
//
// Everything here is pure -- no sockets, no threads, no shared state -- so it
// can be exercised directly by tests, including with deliberately malformed
// input. NinjamClient keeps the stateful dispatch; only the wire format lives
// here.
//
// All multi-byte integers in the Ninjam protocol are LITTLE-ENDIAN
// (justinfrankel/ninjam mpb.cpp:192-195 for bpm/bpi, :281-282 for volume).

namespace chalkwalk::ninjam::protocol {

enum Msg : std::uint8_t {
  ServerAuthChallenge = 0x00,
  ServerAuthReply = 0x01,
  ServerConfigChange = 0x02,
  ServerUserInfoChange = 0x03,
  DownloadIntervalBegin = 0x04,
  DownloadIntervalWrite = 0x05,
  ClientAuthUser = 0x80,
  ClientSetUsermask = 0x81,
  ClientSetChannelInfo = 0x82,
  UploadIntervalBegin = 0x83,
  UploadIntervalWrite = 0x84,
  ChatMessage = 0xC0,
  KeepAlive = 0xFD
};

// ---------------------------------------------------------------------------
// Framing: 1-byte type + 4-byte little-endian payload length.
// ---------------------------------------------------------------------------

static constexpr int kHeaderSize = 5;
static constexpr std::uint32_t kMaxPayload = 10u * 1024 * 1024;

struct FrameHeader {
  std::uint8_t type = 0;
  std::uint32_t length = 0;
};

void writeFrameHeader(std::uint8_t out[kHeaderSize], std::uint8_t type,
                      std::uint32_t length);

// Rejects lengths above kMaxPayload.
bool readFrameHeader(const void *fiveBytes, FrameHeader &out);

// ---------------------------------------------------------------------------
// Bounds-checked cursor. Every accessor returns false and leaves the output
// untouched if the read would run past the end; once a read fails the cursor
// latches failed so callers may check ok() once at the end instead of after
// every field.
// ---------------------------------------------------------------------------

class Reader {
public:
  Reader(const void *data, size_t size) noexcept;

  bool u8(std::uint8_t &out) noexcept;
  bool i8(std::int8_t &out) noexcept;
  bool u16le(std::uint16_t &out) noexcept;
  bool i16le(std::int16_t &out) noexcept;
  bool u32le(std::uint32_t &out) noexcept;
  bool bytes(void *dest, size_t n) noexcept;
  bool skip(size_t n) noexcept;

  // Reads up to the next NUL. Fails, without advancing, if no NUL appears
  // before the end of the payload. This is what keeps a truncated or hostile
  // record from walking off the end of the buffer.
  bool cstr(std::string &out) noexcept;

  const void *rest() const noexcept { return p; }
  size_t remaining() const noexcept { return (size_t)(end - p); }
  bool ok() const noexcept { return !failed; }
  bool atEnd() const noexcept { return p >= end; }

private:
  bool need(size_t n) noexcept;

  const std::uint8_t *p;
  const std::uint8_t *end;
  bool failed = false;
};

// ---------------------------------------------------------------------------
// Parsed message forms.
// ---------------------------------------------------------------------------

struct AuthChallenge {
  std::uint8_t challenge[8] = {};
};

struct AuthReply {
  bool granted = false;
  std::string errorMessage;
  // Maximum local channel index the server will accept. The reference client
  // refuses to transmit on any channel at or above this
  // (justinfrankel/ninjam njclient.cpp:1096, :1476), so a server that
  // omits it gets no audio at all from a stock client. Absent on older
  // servers, in which case it stays 0.
  int maxChannels = 0;
};

struct ServerConfig {
  int bpm = 0;
  int bpi = 0;
};

struct UserInfoEntry {
  bool active = false;
  int channelIndex = 0;
  int volume = 0;
  int pan = 0;
  std::uint8_t flags = 0;
  std::string username;
  std::string channelName;
};

struct IntervalBegin {
  std::uint8_t guid[16] = {};
  std::string guidHex;
  std::uint32_t estimatedSize = 0;
  char fourcc[4] = {};
  int channelIndex = 0;
  std::string username; // empty for the 0x83 upload form
  bool isOggAudio() const;
};

struct IntervalWrite {
  std::uint8_t guid[16] = {};
  std::string guidHex;
  bool isFinal = false;
  const void *audioData = nullptr; // view into the caller's payload
  int audioSize = 0;
};

struct Chat {
  std::string type, p1, p2, p3, p4;
};

// The client-sent messages, which only a server has any reason to read. They
// live here with the rest of the wire format so they get the same bounds
// checking and the same truncation sweep; PracticeServer holds the state.
struct AuthUser {
  std::uint8_t hash[20] = {};
  std::string username;
  std::uint32_t caps = 0;
  std::uint32_t version = 0;
};

// Which channels of which player this client wants sent to it. A player absent
// from the list has not been subscribed to.
struct UsermaskEntry {
  std::string username;
  std::uint32_t mask = 0;
};

struct ChannelInfoEntry {
  std::string name;
  int volume = 0;
  int pan = 0;
  std::uint8_t flags = 0;
};

// Each returns false on malformed input, having read nothing past the payload.
bool parseAuthChallenge(const ByteBuffer &payload, AuthChallenge &out);
bool parseAuthReply(const ByteBuffer &payload, AuthReply &out);
bool parseServerConfig(const ByteBuffer &payload, ServerConfig &out);

// Returns false if any record is malformed. Records successfully parsed before
// the failure are retained in `out`, matching the reference server's forgiving
// treatment of trailing garbage.
bool parseUserInfo(const ByteBuffer &payload,
                   std::vector<UserInfoEntry> &out);

// Handles both DOWNLOAD_INTERVAL_BEGIN (0x04, with username) and
// UPLOAD_INTERVAL_BEGIN (0x83, exactly 25 bytes, no username).
bool parseIntervalBegin(const ByteBuffer &payload, IntervalBegin &out);

// Handles both 0x05 and 0x84 -- the payload layouts are identical.
bool parseIntervalWrite(const ByteBuffer &payload, IntervalWrite &out);

bool parseChat(const ByteBuffer &payload, Chat &out);

bool parseAuthUser(const ByteBuffer &payload, AuthUser &out);

// As with parseUserInfo, entries read before a malformed one are retained.
bool parseUsermask(const ByteBuffer &payload,
                   std::vector<UsermaskEntry> &out);

// The leading 2-byte mpisize gives the per-channel metadata width, which is 4
// in every client seen but is honoured rather than assumed -- a wrong guess
// desynchronises the parse for every channel after the first.
bool parseChannelInfo(const ByteBuffer &payload,
                      std::vector<ChannelInfoEntry> &out);

// ---------------------------------------------------------------------------
// Builders.
// ---------------------------------------------------------------------------

// Ninjam challenge-response: SHA1(SHA1(user + ":" + pass) + challenge[0..8]).
void computeAuthHash(const std::string &username, const std::string &password,
                     const std::uint8_t challenge[8], std::uint8_t out[20]);

// Server side, used by the test fixtures. A real server always sends the
// channel cap; omitting it stops a stock client transmitting entirely.
ByteBuffer buildAuthReply(bool granted,
                                 const std::string &errorMessage = {},
                                 int maxChannels = 32);

ByteBuffer buildAuthChallenge(const std::uint8_t challenge[8],
                                     std::uint32_t caps = 0,
                                     std::uint32_t version = 0x00020000,
                                     const std::string &licence = {});

ByteBuffer buildServerConfig(int bpm, int bpi);

ByteBuffer buildUserInfo(const std::vector<UserInfoEntry> &entries);

ByteBuffer buildAuthUser(const std::uint8_t hash[20],
                                const std::string &username,
                                std::uint32_t caps = 1,
                                std::uint32_t version = 0x00020000);

// Channel indices >= 32 are dropped rather than shifted (1u << 32 is UB).
ByteBuffer
buildUsermask(const std::vector<std::pair<std::string, std::uint32_t>> &masks);

ByteBuffer buildChannelInfo(const std::vector<std::string> &names);

// Pass an empty username for the 0x83 upload form (exactly 25 bytes).
ByteBuffer buildIntervalBegin(const std::uint8_t guid[16],
                                     std::uint32_t estimatedSize,
                                     const char fourcc[4], int channelIndex,
                                     const std::string &username = {});

ByteBuffer buildIntervalWrite(const std::uint8_t guid[16], bool isFinal,
                                     const void *audio, int audioSize);

ByteBuffer buildChat(const std::string &type,
                            const std::string &p1 = {},
                            const std::string &p2 = {},
                            const std::string &p3 = {},
                            const std::string &p4 = {});

std::string guidToHex(const std::uint8_t guid[16]);

} // namespace chalkwalk::ninjam::protocol
