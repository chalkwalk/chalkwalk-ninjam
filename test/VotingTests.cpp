// SPDX-License-Identifier: MIT
#include "JuceUnitShim.h"

#include <chalkwalk/ninjam/Voting.h>

#include <cmath>

// The arithmetic only. Nothing here opens a socket or parses a line -- what is
// asserted is the rounding, which is the part every hand-written copy of this
// rule has got wrong.

namespace {

using namespace chalkwalk::ninjam::voting;

class VotingTests : public shim::UnitTest {
public:
  VotingTests() : shim::UnitTest("Voting", "Voting") {}

  void runTest() override {

    beginTest("the shipped default is voting OFF");
    expect(!enabled(kDefaultThresholdPercent));
    expect(!enabled(0));
    expect(!enabled(101));
    expect(enabled(1));
    expect(enabled(50));
    expect(enabled(100));

    beginTest("round to nearest, half up -- not a ceiling");
    // The rounding is the whole content of this function, so it is pinned
    // against an independent expression rather than against itself: nearest
    // integer, halves up, done in floating point.
    for (int users = 0; users <= 64; ++users)
      for (int t = 1; t <= 100; ++t)
        expectEquals(votesRequired(users, t),
                     (int)std::floor(users * t / 100.0 + 0.5),
                     "users " + std::to_string(users) + " at " +
                         std::to_string(t) + "%");

    beginTest("the requirement can be less than half the room");
    // Both of these look wrong and are not. A ceiling would say 3 for each,
    // and a room using one would refuse changes this server accepts.
    expectEquals(votesRequired(4, 60), 2); // 2.4 -> 2
    expectEquals(votesRequired(5, 50), 3); // 2.5 -> 3, the half going up
    expectEquals(votesRequired(2, 50), 1); // one of two carries it alone
    expectEquals(votesRequired(2, 60), 1); // and still does at 60%
    expectEquals(votesRequired(7, 60), 4); // a ceiling would say 5

    beginTest("one vote always carries for a lone user");
    // Below 50% the requirement for a single user is zero, which is not a
    // special case anywhere: the server refuses to tally at all until some
    // vote exists (`bpms[maxbpm] > 0`), so the first vote is what carries.
    for (int t = 1; t <= 100; ++t)
      expect(votesRequired(1, t) <= 1, "threshold " + std::to_string(t));

    beginTest("nobody needs nothing");
    expectEquals(votesRequired(0, 50), 0);

    beginTest("unanimity is required at 100% and never more than that");
    for (int users = 1; users <= 12; ++users)
      expectEquals(votesRequired(users, 100), users);

    beginTest("the requirement never exceeds the room");
    for (int users = 0; users <= 32; ++users)
      for (int t = 1; t <= 100; ++t)
        expect(votesRequired(users, t) <= users || users == 0);

    beginTest("a vote is live for the whole timeout and stale after");
    expect(isLive(100, 100, 60));
    expect(isLive(100, 160, 60), "the last live second");
    expect(!isLive(100, 161, 60));
    expect(!isLive(0, 100, 60), "never voted");

    beginTest("the leader is the most-voted value");
    Tally t;
    expect(!t.any());
    expectEquals(t.votes(), 0);
    t.add(130);
    t.add(140);
    t.add(140);
    expectEquals(t.leader(), 140);
    expectEquals(t.votes(), 2);

    beginTest("a tie keeps the value that got there first");
    // The server moves its leader only on a strict '>', scanning users in
    // order, so the value that reached the count first holds it.
    t.add(130);
    expectEquals(t.leader(), 140, "130 tied at 2 and must not take the lead");
    expectEquals(t.votes(), 2);
    t.add(130);
    expectEquals(t.leader(), 130, "3 beats 2");

    beginTest("carrying is the requirement, evaluated on the whole room");
    Tally two;
    two.add(120);
    expect(two.carries(2, 50), "one of two, at 50%");
    expect(two.carries(2, 60), "and at 60% -- 1.2 rounds to 1");
    expect(!two.carries(2, 80), "1.6 rounds to 2, so one is not enough");
    expect(!two.carries(5, 50), "one of five is not enough anywhere");

    beginTest("an empty tally carries nothing, however small the room");
    Tally none;
    expect(!none.carries(1, 1));
    expect(!none.carries(0, 50));
  }
};

TEST_CASE("voting") {
  VotingTests t;
  t.runTest();
}

} // namespace
