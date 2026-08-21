// SPDX-License-Identifier: MIT
#include "JuceUnitShim.h"

#include <chalkwalk/ninjam/RoomConventions.h>

// Text in, text out. Nothing here knows what a key is -- that is the seam that
// keeps this library free of a music-theory dependency.

namespace {

using namespace chalkwalk::ninjam::conventions;

class RoomConventionsTests : public shim::UnitTest {
public:
  RoomConventionsTests()
      : shim::UnitTest("RoomConventions", "RoomConventions") {}

  void runTest() override {

    // --- the key tag ---
  beginTest("what we build is what we read");
  for (const char *name : {"D minor", "Bb major", "F# dorian", "C"}) {
    const auto line = buildKeyTag(name);
    expectEquals(extractKeyTag(line), std::string(name));
    expectEquals(extractKeyAnnouncement(line), std::string(name));
  }

  beginTest("an empty name builds nothing rather than an empty tag");
  expect(buildKeyTag("").empty());

  beginTest("the tag is found wherever it sits");
  // It has to be, or it could not ride in a room topic alongside other text.
  expectEquals(extractKeyTag("welcome all [key: G minor] have fun"),
               std::string("G minor"));
  expectEquals(extractKeyTag("[KEY: A major]"), std::string("A major"));

  beginTest("a malformed tag yields nothing rather than a guess");
  for (const char *bad : {"[key: D minor", "key: D minor]", "no tag here",
                          "[ke y: D minor]", ""})
    expect(extractKeyTag(bad).empty(), std::string("read a tag out of: ") + bad);

    // --- the sayable form ---
  beginTest("the slash form is line-leading only");
  // Matched anywhere it would be unsayable, which is the whole reason the tag
  // is not the only form.
  expectEquals(extractKeyAnnouncement("/key D minor"), std::string("D minor"));
  expectEquals(extractKeyAnnouncement("  /key D minor  "), std::string("D minor"));
  expect(extractKeyAnnouncement("type /key D minor to change it").empty(),
         "a mid-line slash form set the key");

  beginTest("the advice line can be quoted without performing itself");
  // The property the whole two-form design exists for: a bot telling somebody
  // how to change the key must not change it.
  const auto advice = keyAdviceLine("D minor");
  expect(advice.find(keyTagPrefix()) == std::string::npos,
         "the advice line carries the tag: " + advice);
  expect(extractKeyAnnouncement("say \"" + advice + "\" to change it").empty(),
         "quoting the advice line set the key");

  beginTest("prose is never a key");
  for (const char *prose : {"the key is stuck", "keys are hard", "/keyboard",
                            "monkey business"})
    expect(extractKeyAnnouncement(prose).empty(),
           std::string("read a key out of: ") + prose);

    // --- what the server will take ---
  beginTest("the vote ranges are the stock server's");
  expect(isVotableBpm(40) && isVotableBpm(400));
  expect(!isVotableBpm(39) && !isVotableBpm(401));
  expect(isVotableBpi(2) && isVotableBpi(64));
  expect(!isVotableBpi(1) && !isVotableBpi(65));

  beginTest("an admin may set what a vote may not");
  // Wider at both ends, which is why they are separate questions.
  expect(isAdminSettableBpm(20) && !isVotableBpm(20));
  expect(isAdminSettableBpi(1024) && !isVotableBpi(1024));
  }
};

TEST_CASE("room conventions") {
  RoomConventionsTests t;
  t.runTest();
}

} // namespace
