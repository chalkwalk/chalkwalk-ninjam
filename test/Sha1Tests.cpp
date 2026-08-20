#include "JuceUnitShim.h"

#include <chalkwalk/ninjam/Sha1.h>

#include <string>

using namespace chalkwalk::ninjam;

namespace {

std::string toHex(const uint8_t digest[20]) {
  std::string s;
  for (int i = 0; i < 20; ++i) {
    s.push_back("0123456789abcdef"[(digest[i] >> 4) & 0x0F]);
    s.push_back("0123456789abcdef"[digest[i] & 0x0F]);
  }
  return s;
}

std::string hashOf(const std::string &input) {
  Sha1 sha;
  sha.add(input.data(), (int)input.size());
  uint8_t digest[20];
  sha.result(digest);
  return toHex(digest);
}

class Sha1Tests : public shim::UnitTest {
public:
  Sha1Tests() : shim::UnitTest("Sha1", "Sha1") {}

  void runTest() override {
    beginTest("FIPS 180-1 vectors");
    expectEquals(hashOf("abc"),
                 std::string("a9993e364706816aba3e25717850c26c9cd0d89d"));
    expectEquals(
        hashOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
        std::string("84983e441c3bd26ebaae4aa1f95129e5e54670f1"));
    expectEquals(hashOf(std::string(1000000, 'a')),
                 std::string("34aa973cd4c4daa4f61eeb2bdbad27316534016f"));

    beginTest("empty input");
    expectEquals(hashOf(""),
                 std::string("da39a3ee5e6b4b0d3255bfef95601890afd80709"));

    beginTest("incremental add equals monolithic");
    // The auth path feeds SHA1 in several add() calls, so this invariant is
    // load-bearing. Exercise every split point, including across the internal
    // 64-byte block boundary.
    const std::string msg =
        "the quick brown fox jumps over the lazy dog, repeatedly, until this "
        "string is comfortably longer than one sha1 block of sixty-four bytes";
    const std::string whole = hashOf(msg);
    for (size_t split = 0; split <= msg.size(); ++split) {
      Sha1 sha;
      sha.add(msg.data(), (int)split);
      sha.add(msg.data() + split, (int)(msg.size() - split));
      uint8_t digest[20];
      sha.result(digest);
      if (toHex(digest) != whole) {
        expect(false, "split at " + std::to_string(split) + " differs");
        break;
      }
    }
    expect(true);

    beginTest("result() resets state for reuse");
    Sha1 sha;
    sha.add("abc", 3);
    uint8_t first[20];
    sha.result(first);
    sha.add("abc", 3);
    uint8_t second[20];
    sha.result(second);
    expectEquals(toHex(second), toHex(first));

    beginTest("zero-length add is a no-op");
    Sha1 a;
    a.add("abc", 3);
    a.add("", 0);
    uint8_t d[20];
    a.result(d);
    expectEquals(toHex(d),
                 std::string("a9993e364706816aba3e25717850c26c9cd0d89d"));
  }
};

TEST_CASE("sha1") {
  Sha1Tests t;
  t.runTest();
}

} // namespace
