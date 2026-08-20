#include "JuceUnitShim.h"

#include <chalkwalk/ninjam/NinjamProtocol.h>

#include <cstdint>
#include <vector>

namespace {

using namespace chalkwalk::ninjam::protocol;
using namespace chalkwalk::ninjam;

chalkwalk::ninjam::ByteBuffer mb(std::initializer_list<int> bytes) {
  chalkwalk::ninjam::ByteBuffer b;
  for (int v : bytes) {
    const std::uint8_t x = (std::uint8_t)v;
    chalkwalk::ninjam::appendBytes(b, &x, 1);
  }
  return b;
}

std::string hex(const std::uint8_t *d, int n) {
  std::string s;
  for (int i = 0; i < n; ++i) {
    s.push_back("0123456789abcdef"[(d[i] >> 4) & 0x0F]);
    s.push_back("0123456789abcdef"[d[i] & 0x0F]);
  }
  return s;
}

class NinjamProtocolTests : public shim::UnitTest {
public:
  NinjamProtocolTests() : shim::UnitTest("NinjamProtocol", "NinjamProtocol") {}

  // Feeds every strict prefix of a valid payload to a parser and requires that
  // none of them is accepted as complete without also being safe. Run this
  // binary under ASan to turn any over-read into a hard failure.
  template <typename Fn>
  void truncationSweep(const chalkwalk::ninjam::ByteBuffer &valid, Fn &&parse,
                       const std::string &what) {
    for (size_t n = 0; n < valid.size(); ++n) {
      chalkwalk::ninjam::ByteBuffer prefix(valid.data(), valid.data() + n);
      parse(prefix); // must not read out of bounds, must not crash
    }
    expect(true, what + " survived truncation sweep");
  }

  void runTest() override {
    runFramingTests();
    runReaderTests();
    runParserTests();
    runTruncationTests();
    runBuilderTests();
    runClampTests();
    runAuthTests();
  }

  void runFramingTests() {
    beginTest("frame header round-trip, little-endian length");
    {
      std::uint8_t h[kHeaderSize];
      writeFrameHeader(h, 0xC0, 0x00030201);
      expectEquals((int)h[0], 0xC0);
      expectEquals((int)h[1], 0x01);
      expectEquals((int)h[2], 0x02);
      expectEquals((int)h[3], 0x03);
      expectEquals((int)h[4], 0x00);

      FrameHeader out;
      expect(readFrameHeader(h, out));
      expectEquals((int)out.type, 0xC0);
      expectEquals((int)out.length, 0x00030201);

      // Zero-length payloads (KEEP_ALIVE) round-trip too.
      writeFrameHeader(h, 0xFD, 0);
      expect(readFrameHeader(h, out));
      expectEquals((int)out.type, 0xFD);
      expectEquals((int)out.length, 0);
    }

    beginTest("frame header rejects oversized length");
    {
      std::uint8_t h[kHeaderSize];
      writeFrameHeader(h, 0x05, kMaxPayload + 1);
      FrameHeader out;
      expect(!readFrameHeader(h, out));

      writeFrameHeader(h, 0x05, kMaxPayload);
      expect(readFrameHeader(h, out));
    }
  }

  void runReaderTests() {
    beginTest("Reader refuses to read past the end");
    {
      const std::uint8_t data[3] = {1, 2, 3};
      Reader r(data, 3);
      std::uint32_t v32;
      expect(!r.u32le(v32), "read 4 bytes from a 3-byte buffer");
      expect(!r.ok(), "cursor should latch failed");

      Reader r2(data, 3);
      std::uint8_t a, b, c, d;
      expect(r2.u8(a) && r2.u8(b) && r2.u8(c));
      expect(!r2.u8(d));
    }

    beginTest("Reader::cstr requires a terminator inside the payload");
    {
      const char unterminated[4] = {'a', 'b', 'c', 'd'};
      Reader r(unterminated, 4);
      std::string s;
      expect(!r.cstr(s), "accepted a string with no NUL");

      const char terminated[4] = {'a', 'b', 'c', '\0'};
      Reader r2(terminated, 4);
      expect(r2.cstr(s));
      expectEquals(s, std::string("abc"));
      expect(r2.atEnd());
    }

    beginTest("Reader signed conversions");
    {
      auto p = mb({0xFF, 0xFF, 0x80});
      Reader r(p.data(), p.size());
      std::int16_t v16;
      std::int8_t v8;
      expect(r.i16le(v16));
      expectEquals((int)v16, -1);
      expect(r.i8(v8));
      expectEquals((int)v8, -128);
    }

    beginTest("Reader on empty and null buffers");
    {
      Reader r(nullptr, 0);
      std::uint8_t v;
      expect(!r.u8(v));
      expect(r.atEnd());
    }
  }

  void runParserTests() {
    beginTest("0x02 server config is little-endian");
    {
      // bpm = 120 (0x0078), bpi = 16 (0x0010), both little-endian.
      auto p = mb({0x78, 0x00, 0x10, 0x00});
      ServerConfig cfg;
      expect(parseServerConfig(p, cfg));
      expectEquals(cfg.bpm, 120);
      expectEquals(cfg.bpi, 16);
    }

    beginTest("the server's tempo is followed, never validated");
    {
      // What a client may VOTE for and what a server may BE are different
      // ranges -- `!vote` allows 40..400 BPM and 2..64 BPI, while an admin may
      // set 20..400 and 2..1024 (docs/PROTOCOL.md). So a room can legitimately
      // sit at values no client could have proposed, and every client has to
      // follow it there.
      //
      // Both numbers below were observed on a live server. JamTaba shows
      // neither correctly: `ServerInfo::setBpm` drops an out-of-range value
      // with no else branch (elieserdejesus/JamTaba
      // src/Common/ninjam/client/ServerInfo.cpp:112-123), so it silently keeps
      // displaying the previous tempo. That is the bug this test exists to
      // stop us reinventing -- clamping incoming config to the vote range
      // looks like validation and is a lie about the room.
      const struct { int bpm, bpi; } kReal[] = {
          {39, 124},  // below the vote minimum, above the vote maximum
          {20, 1024}, // the admin extremes
          {400, 2},
      };
      for (const auto &c : kReal) {
        ServerConfig cfg;
        expect(parseServerConfig(buildServerConfig(c.bpm, c.bpi), cfg),
               "round trip failed for " + std::to_string(c.bpm) + "/" +
                   std::to_string(c.bpi));
        expectEquals(cfg.bpm, c.bpm);
        expectEquals(cfg.bpi, c.bpi);
      }
    }

    beginTest("0x01 auth reply");
    {
      AuthReply r;
      expect(parseAuthReply(mb({1}), r));
      expect(r.granted);
      expect(parseAuthReply(mb({0}), r));
      expect(!r.granted);
      expect(!parseAuthReply(chalkwalk::ninjam::ByteBuffer(), r));
    }

    beginTest("0x03 user info round-trip with signed volume and pan");
    {
      chalkwalk::ninjam::ByteBuffer p;
      const std::uint8_t head[6] = {1, 2, 0xFF, 0xFF, 0x80, 0x00};
      chalkwalk::ninjam::appendBytes(p, head, 6); // active, chIdx=2, volume=-1, pan=-128, flags=0
      chalkwalk::ninjam::appendBytes(p, "alice\0", 6);
      chalkwalk::ninjam::appendBytes(p, "gtr\0", 4);

      std::vector<UserInfoEntry> entries;
      expect(parseUserInfo(p, entries));
      expectEquals((int)entries.size(), 1);
      expect(entries[0].active);
      expectEquals(entries[0].channelIndex, 2);
      expectEquals(entries[0].volume, -1);
      expectEquals(entries[0].pan, -128);
      expectEquals(entries[0].username, std::string("alice"));
      expectEquals(entries[0].channelName, std::string("gtr"));
    }

    beginTest("0x03 rejects a record with only four header bytes left");
    {
      // The fixed part of a record is six bytes. The previous implementation
      // checked for four and then read six, running two bytes past the end.
      chalkwalk::ninjam::ByteBuffer p;
      const std::uint8_t head[4] = {1, 0, 0, 0};
      chalkwalk::ninjam::appendBytes(p, head, 4);
      std::vector<UserInfoEntry> entries;
      expect(!parseUserInfo(p, entries), "accepted a 4-byte record");
    }

    beginTest("0x03 rejects an unterminated username");
    {
      chalkwalk::ninjam::ByteBuffer p;
      const std::uint8_t head[6] = {1, 0, 0, 0, 0, 0};
      chalkwalk::ninjam::appendBytes(p, head, 6);
      chalkwalk::ninjam::appendBytes(p, "alice", 5); // no NUL: the old code walked off the heap here
      std::vector<UserInfoEntry> entries;
      expect(!parseUserInfo(p, entries), "accepted an unterminated username");
    }

    beginTest("0x03 keeps entries parsed before a malformed record");
    {
      chalkwalk::ninjam::ByteBuffer p;
      const std::uint8_t head[6] = {1, 0, 0, 0, 0, 0};
      chalkwalk::ninjam::appendBytes(p, head, 6);
      chalkwalk::ninjam::appendBytes(p, "bob\0", 4);
      chalkwalk::ninjam::appendBytes(p, "ch\0", 3);
      chalkwalk::ninjam::appendBytes(p, head, 3); // truncated second record
      std::vector<UserInfoEntry> entries;
      expect(!parseUserInfo(p, entries));
      expectEquals((int)entries.size(), 1);
      expectEquals(entries[0].username, std::string("bob"));
    }

    beginTest("0x04 download interval begin");
    {
      std::uint8_t guid[16];
      for (int i = 0; i < 16; ++i)
        guid[i] = (std::uint8_t)(i * 17);
      const char fourcc[4] = {'O', 'G', 'G', 'v'};
      auto p = buildIntervalBegin(guid, 4096, fourcc, 3, "carol");

      IntervalBegin b;
      expect(parseIntervalBegin(p, b));
      expectEquals((int)b.estimatedSize, 4096);
      expectEquals(b.channelIndex, 3);
      expectEquals(b.username, std::string("carol"));
      expect(b.isOggAudio());
      expectEquals(b.guidHex, hex(guid, 16));
    }

    beginTest("0x83 upload interval begin is exactly 25 bytes, no username");
    {
      std::uint8_t guid[16] = {};
      const char fourcc[4] = {'O', 'G', 'G', 'v'};
      auto p = buildIntervalBegin(guid, 0, fourcc, 1);
      expectEquals((int)p.size(), 25, "servers reject a longer 0x83");

      IntervalBegin b;
      expect(parseIntervalBegin(p, b));
      expectEquals(b.channelIndex, 1);
      expect(b.username.empty());
    }

    beginTest("non-OGGv fourcc is reported as non-audio");
    {
      std::uint8_t guid[16] = {};
      const char jtbv[4] = {'J', 'T', 'B', 'v'}; // Jamtaba video
      auto p = buildIntervalBegin(guid, 0, jtbv, 1, "dave");
      IntervalBegin b;
      expect(parseIntervalBegin(p, b));
      expect(!b.isOggAudio());
    }

    beginTest("0x05 interval write flags and payload view");
    {
      std::uint8_t guid[16] = {};
      guid[0] = 0xAB;
      const std::uint8_t audio[5] = {1, 2, 3, 4, 5};

      auto p = buildIntervalWrite(guid, false, audio, 5);
      IntervalWrite w;
      expect(parseIntervalWrite(p, w));
      expect(!w.isFinal);
      expectEquals(w.audioSize, 5);
      expect(memcmp(w.audioData, audio, 5) == 0);

      auto q = buildIntervalWrite(guid, true, nullptr, 0);
      expectEquals((int)q.size(), 17);
      expect(parseIntervalWrite(q, w));
      expect(w.isFinal);
      expectEquals(w.audioSize, 0);
    }

    beginTest("0xC0 chat round-trip and optional trailing fields");
    {
      auto p = buildChat("PRIVMSG", "alice", "hi there");
      Chat c;
      expect(parseChat(p, c));
      expectEquals(c.type, std::string("PRIVMSG"));
      expectEquals(c.p1, std::string("alice"));
      expectEquals(c.p2, std::string("hi there"));
      expect(c.p3.empty());
      expect(c.p4.empty());

      // A sender that stops after two fields is legal.
      chalkwalk::ninjam::ByteBuffer q;
      chalkwalk::ninjam::appendBytes(q, "MSG\0", 4);
      chalkwalk::ninjam::appendBytes(q, "bob\0", 4);
      expect(parseChat(q, c));
      expectEquals(c.type, std::string("MSG"));
      expectEquals(c.p1, std::string("bob"));
      expect(c.p2.empty());
    }

    beginTest("0xC0 rejects a present-but-unterminated field");
    {
      chalkwalk::ninjam::ByteBuffer p;
      chalkwalk::ninjam::appendBytes(p, "MSG\0", 4);
      chalkwalk::ninjam::appendBytes(p, "bob", 3); // started a field, never terminated it
      Chat c;
      expect(!parseChat(p, c));
    }

    beginTest("0x80 round-trips, and the tail is optional");
    {
      std::uint8_t hash[20];
      for (int i = 0; i < 20; ++i)
        hash[i] = (std::uint8_t)(i + 7);

      AuthUser a;
      expect(parseAuthUser(buildAuthUser(hash, "alice", 1, 0x00020000), a));
      expectEquals(a.username, std::string("alice"));
      expectEquals((int)a.caps, 1);
      expectEquals((int)a.version, 0x00020000);
      expect(memcmp(a.hash, hash, 20) == 0);

      // A client that stops after the username is still understood.
      chalkwalk::ninjam::ByteBuffer short_;
      chalkwalk::ninjam::appendBytes(short_, hash, 20);
      chalkwalk::ninjam::appendBytes(short_, "bob\0", 4);
      AuthUser b;
      expect(parseAuthUser(short_, b));
      expectEquals(b.username, std::string("bob"));
      expectEquals((int)b.caps, 0);
    }

    beginTest("0x81 round-trips, and an empty mask is not an absent one");
    {
      std::vector<UsermaskEntry> m;
      expect(parseUsermask(buildUsermask({{"alice", 0x5u}, {"bob", 0u}}), m));
      expectEquals((int)m.size(), 2);
      expectEquals(m[0].username, std::string("alice"));
      expectEquals((int)m[0].mask, 5);
      // Subscribed to nothing, but present -- which is how a bot goes deaf.
      expectEquals(m[1].username, std::string("bob"));
      expectEquals((int)m[1].mask, 0);

      std::vector<UsermaskEntry> none;
      expect(parseUsermask({}, none));
      expectEquals((int)none.size(), 0);
    }

    beginTest("0x82 round-trips and honours mpisize");
    {
      std::vector<ChannelInfoEntry> c;
      expect(parseChannelInfo(buildChannelInfo({"gtr", "vox"}), c));
      expectEquals((int)c.size(), 2);
      expectEquals(c[0].name, std::string("gtr"));
      expectEquals(c[1].name, std::string("vox"));

      // A wider metadata block must be skipped, not misread as the next name.
      chalkwalk::ninjam::ByteBuffer wide;
      const std::uint8_t mpisize[2] = {6, 0};
      chalkwalk::ninjam::appendBytes(wide, mpisize, 2);
      chalkwalk::ninjam::appendBytes(wide, "gtr\0", 4);
      const std::uint8_t meta[6] = {0, 0, 0, 0, 0xAA, 0xBB};
      chalkwalk::ninjam::appendBytes(wide, meta, 6);
      chalkwalk::ninjam::appendBytes(wide, "vox\0", 4);
      chalkwalk::ninjam::appendBytes(wide, meta, 6);
      std::vector<ChannelInfoEntry> w;
      expect(parseChannelInfo(wide, w));
      expectEquals((int)w.size(), 2);
      expectEquals(w[1].name, std::string("vox"));
    }
  }

  void runTruncationTests() {
    beginTest("every parser survives every truncation");
    // The parsers must be total. Any over-read here is a heap read past the end
    // of a MemoryBlock that is sized to exactly the payload length and is not
    // NUL-padded.
    std::uint8_t guid[16];
    for (int i = 0; i < 16; ++i)
      guid[i] = (std::uint8_t)(i + 1);
    const char fourcc[4] = {'O', 'G', 'G', 'v'};
    const std::uint8_t audio[8] = {1, 2, 3, 4, 5, 6, 7, 8};

    chalkwalk::ninjam::ByteBuffer userInfo;
    const std::uint8_t head[6] = {1, 0, 0x10, 0x00, 0x20, 0x00};
    chalkwalk::ninjam::appendBytes(userInfo, head, 6);
    chalkwalk::ninjam::appendBytes(userInfo, "alice\0", 6);
    chalkwalk::ninjam::appendBytes(userInfo, "guitar\0", 7);
    chalkwalk::ninjam::appendBytes(userInfo, head, 6);
    chalkwalk::ninjam::appendBytes(userInfo, "bob\0", 4);
    chalkwalk::ninjam::appendBytes(userInfo, "bass\0", 5);

    truncationSweep(
        mb({1, 2, 3, 4, 5, 6, 7, 8}),
        [](const chalkwalk::ninjam::ByteBuffer &p) {
          AuthChallenge c;
          parseAuthChallenge(p, c);
        },
        "0x00");
    truncationSweep(
        mb({1}),
        [](const chalkwalk::ninjam::ByteBuffer &p) {
          AuthReply r;
          parseAuthReply(p, r);
        },
        "0x01");
    truncationSweep(
        mb({0x78, 0x00, 0x10, 0x00}),
        [](const chalkwalk::ninjam::ByteBuffer &p) {
          ServerConfig c;
          parseServerConfig(p, c);
        },
        "0x02");
    truncationSweep(
        userInfo,
        [](const chalkwalk::ninjam::ByteBuffer &p) {
          std::vector<UserInfoEntry> e;
          parseUserInfo(p, e);
        },
        "0x03");
    truncationSweep(
        buildIntervalBegin(guid, 1234, fourcc, 2, "alice"),
        [](const chalkwalk::ninjam::ByteBuffer &p) {
          IntervalBegin b;
          parseIntervalBegin(p, b);
        },
        "0x04");
    truncationSweep(
        buildIntervalWrite(guid, true, audio, 8),
        [](const chalkwalk::ninjam::ByteBuffer &p) {
          IntervalWrite w;
          parseIntervalWrite(p, w);
        },
        "0x05");
    truncationSweep(
        buildChat("PRIVMSG", "alice", "hello", "x", "y"),
        [](const chalkwalk::ninjam::ByteBuffer &p) {
          Chat c;
          parseChat(p, c);
        },
        "0xC0");

    std::uint8_t authHash[20];
    for (int i = 0; i < 20; ++i)
      authHash[i] = (std::uint8_t)(i * 3 + 1);
    truncationSweep(
        buildAuthUser(authHash, "alice"),
        [](const chalkwalk::ninjam::ByteBuffer &p) {
          AuthUser a;
          parseAuthUser(p, a);
        },
        "0x80");
    truncationSweep(
        buildUsermask({{"alice", 0x3u}, {"bob", 0x1u}}),
        [](const chalkwalk::ninjam::ByteBuffer &p) {
          std::vector<UsermaskEntry> m;
          parseUsermask(p, m);
        },
        "0x81");
    truncationSweep(
        buildChannelInfo({"gtr", "vox"}),
        [](const chalkwalk::ninjam::ByteBuffer &p) {
          std::vector<ChannelInfoEntry> c;
          parseChannelInfo(p, c);
        },
        "0x82");

    beginTest("parsers survive random garbage");
    {
      shim::Random rng(1234);
      for (int iter = 0; iter < 2000; ++iter) {
        chalkwalk::ninjam::ByteBuffer p((size_t)rng.nextInt(64));
        for (size_t i = 0; i < p.size(); ++i)
          p[i] = (char)rng.nextInt(256);

        AuthChallenge ac;
        parseAuthChallenge(p, ac);
        AuthReply ar;
        parseAuthReply(p, ar);
        ServerConfig sc;
        parseServerConfig(p, sc);
        std::vector<UserInfoEntry> ui;
        parseUserInfo(p, ui);
        IntervalBegin ib;
        parseIntervalBegin(p, ib);
        IntervalWrite iw;
        parseIntervalWrite(p, iw);
        Chat ch;
        parseChat(p, ch);
      }
      expect(true);
    }
  }

  void runBuilderTests() {
    beginTest("0x80 auth packet layout");
    {
      std::uint8_t hash[20];
      for (int i = 0; i < 20; ++i)
        hash[i] = (std::uint8_t)i;
      auto p = buildAuthUser(hash, "tester");
      expectEquals((int)p.size(), 20 + 7 + 4 + 4);

      const auto *b = static_cast<const std::uint8_t *>(p.data());
      expect(memcmp(b, hash, 20) == 0);
      expect(memcmp(b + 20, "tester\0", 7) == 0);
      // caps = 1 LE, version = 0x00020000 LE
      expectEquals((int)b[27], 1);
      expectEquals((int)b[28], 0);
      expectEquals((int)b[29], 0);
      expectEquals((int)b[30], 0);
      expectEquals((int)b[31], 0);
      expectEquals((int)b[32], 0);
      expectEquals((int)b[33], 0x02);
      expectEquals((int)b[34], 0);
    }

    beginTest("0x81 usermask bitmask layout");
    {
      // Channels 0, 3 and 5 enabled -> 0b101001 = 0x29.
      std::vector<std::pair<std::string, std::uint32_t>> masks{{"alice", 0x29}};
      auto p = buildUsermask(masks);
      expectEquals((int)p.size(), 6 + 4);
      const auto *b = static_cast<const std::uint8_t *>(p.data());
      expect(memcmp(b, "alice\0", 6) == 0);
      expectEquals((int)b[6], 0x29);
      expectEquals((int)b[7], 0);
      expectEquals((int)b[8], 0);
      expectEquals((int)b[9], 0);
    }

    beginTest("0x82 channel info layout with mpisize");
    {
      auto p = buildChannelInfo({"gtr", "bass"});
      // 2 (mpisize) + 4 ("gtr\0") + 4 (meta) + 5 ("bass\0") + 4 (meta)
      expectEquals((int)p.size(), 19);
      const auto *b = static_cast<const std::uint8_t *>(p.data());
      expectEquals((int)b[0], 4);
      expectEquals((int)b[1], 0);
      expect(memcmp(b + 2, "gtr\0", 4) == 0);
      for (int i = 6; i < 10; ++i)
        expectEquals((int)b[i], 0);
      expect(memcmp(b + 10, "bass\0", 5) == 0);
    }

    beginTest("0x82 with no channels is just the mpisize header");
    {
      expectEquals((int)buildChannelInfo({}).size(), 2);
    }
  }

  // -------------------------------------------------------------------
  // The three builder fields that pass through a clamp.
  //
  // All three had the argument order wrong: juce::jlimit takes
  // (lo, hi, value) and std::clamp takes (value, lo, hi), and the port from
  // antiphon transcribed the order rather than the meaning. So
  // std::clamp(0, 255, 32) asked for 0 clamped to the range [255, 32] --
  // undefined behaviour on an inverted range, which libstdc++ catches with an
  // assertion when they are enabled and which otherwise quietly returns 255.
  //
  // None of the three was covered, which is exactly why the suite stayed
  // green through the mistake. The numbers below are the whole test: each one
  // is a value strictly inside its range, so it can only pass if the value
  // rather than a bound comes out the other end.
  // -------------------------------------------------------------------
  void runClampTests() {
    beginTest("clamped fields carry their value, not their bound");

    // maxChannels is the last byte of the auth reply. A server that sends the
    // wrong cap here stops a stock client transmitting on the channels above
    // it, so this byte is load-bearing rather than cosmetic.
    expectEquals((int)buildAuthReply(true, "", 32).back(), 32,
                 "the channel cap must be the value, not the upper bound");
    expectEquals((int)buildAuthReply(true, "", 300).back(), 255,
                 "a cap wider than a byte still saturates");
    expectEquals((int)buildAuthReply(true, "", -5).back(), 0,
                 "a negative cap still floors at zero");

    // Channel index is byte 1 of a user info record; pan is byte 4.
    UserInfoEntry e;
    e.active = true;
    e.channelIndex = 3;
    e.pan = 100;
    e.username = "u";
    e.channelName = "c";

    auto info = buildUserInfo({e});
    expectEquals((int)info[1], 3, "channel index must be the value");
    expectEquals((int)(std::int8_t)info[4], 100, "pan must be the value");

    e.pan = -100;
    expectEquals((int)(std::int8_t)buildUserInfo({e})[4], -100,
                 "a pan hard left survives the clamp");

    e.pan = 0;
    e.channelIndex = 400;
    expectEquals((int)buildUserInfo({e})[1], 255,
                 "a channel index wider than a byte still saturates");
  }

  void runAuthTests() {
    beginTest("auth hash matches an independent SHA1 implementation");
    // Goldens computed with Python hashlib, not with our own Sha1 class:
    //   sha1(sha1(user + ":" + pass) + challenge)
    std::uint8_t challenge[8];
    for (int i = 0; i < 8; ++i)
      challenge[i] = (std::uint8_t)i;

    std::uint8_t out[20];

    computeAuthHash("tester", "", challenge, out);
    expectEquals(hex(out, 20),
                 std::string("0471f0ad9885d825ce678e75cf23668c994068f8"));

    computeAuthHash("alice", "secret", challenge, out);
    expectEquals(hex(out, 20),
                 std::string("7f5c31b13ebe89c36c8e3b5ee59720e238bb6422"));

    // The anonymous login form used by the server browser.
    computeAuthHash("anonymous:bob", "", challenge, out);
    expectEquals(hex(out, 20),
                 std::string("81d28bdad1230452f6ae94f940c9f9ce94b4d0b4"));
  }
};

TEST_CASE("ninjam protocol") {
  NinjamProtocolTests t;
  t.runTest();
}

} // namespace
