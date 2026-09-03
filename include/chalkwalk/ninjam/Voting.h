// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <vector>

// The server's tempo vote, as arithmetic.
//
// ON THE WIRE, but only barely: there is no vote message. A vote is the chat
// line `!vote bpm 130`, and the result is English broadcast back
// (`[voting system] leading candidate: 3/5 votes for 137 BPM`). What is defined
// here is the part that is not English -- who is counted, what carries, and
// when a vote goes stale -- because three different things in this ecosystem
// need it and each was about to write it out again:
//
//   - a server tallying real votes;
//   - a client deciding whether to offer one;
//   - a bot deciding whether to back one, which needs the same formula
//     evaluated on a subset of the room.
//
// Three copies of a rounding rule is how a room disagrees with itself. This is
// the one copy (`PRINCIPLES 8`).
//
// Every value below is read from the reference server at the revision in
// `docs/references/SOURCES.md`, not chosen here.
namespace chalkwalk::ninjam::voting {

// The server ships with voting OFF: 110 is out of range, and out of range is
// how it is disabled (justinfrankel/ninjam server/usercon.cpp:888, :1154).
// A room that wants voting must say so.
inline constexpr int kDefaultThresholdPercent = 110;
inline constexpr int kDefaultTimeoutSeconds = 120;

inline constexpr bool enabled(int thresholdPercent) {
  return thresholdPercent >= 1 && thresholdPercent <= 100;
}

// How many votes carry a change, given how many users are counted.
//
// ROUND HALF UP, not a ceiling: the `+50` is in the server's own expression
// (usercon.cpp:1215, :1239) and the difference is not cosmetic. At 50% in a
// two-person room this returns 1, so one of two people carries a change alone
// -- which is the case that has caught every hand-written version of this rule.
//
// `users` is every authenticated user, whether or not they voted, so declining
// to vote is voting against. Hidden users (PRIV_HIDDEN) are the one exclusion,
// and that is the caller's to apply: this counts what it is given.
inline constexpr int votesRequired(int users, int thresholdPercent) {
  return (users * thresholdPercent + 50) / 100;
}

// Whether a vote cast at `castAt` is still live at `now`, both in seconds on
// whatever clock the caller keeps. The server's test is `>=`, so a vote is live
// for the full timeout and expires on the tick after (usercon.cpp:1202).
inline constexpr bool isLive(std::int64_t castAt, std::int64_t now,
                             int timeoutSeconds) {
  return castAt > 0 && castAt >= now - timeoutSeconds;
}

// The running count of one poll, fed one live vote at a time.
//
// The leader is the value with the most votes, and ties go to whichever was
// ADDED FIRST -- the server scans its user list in order and moves the leader
// only on a strict `>` (usercon.cpp:1204, :1209), so feed votes in the same
// order the server would scan them and the answer matches. It is a detail that
// only shows in a tie, and a tie is exactly when a room notices.
class Tally {
public:
  void add(int value) {
    for (auto &e : entries)
      if (e.value == value) {
        ++e.count;
        if (e.count > bestCount) {
          bestCount = e.count;
          bestValue = e.value;
        }
        return;
      }
    entries.push_back({value, 1});
    if (1 > bestCount) {
      bestCount = 1;
      bestValue = value;
    }
  }

  int leader() const { return bestValue; }
  int votes() const { return bestCount; }
  bool any() const { return bestCount > 0; }

  bool carries(int users, int thresholdPercent) const {
    return any() && bestCount >= votesRequired(users, thresholdPercent);
  }

private:
  struct Entry {
    int value;
    int count;
  };
  std::vector<Entry> entries;
  int bestValue = 0;
  int bestCount = 0;
};

} // namespace chalkwalk::ninjam::voting
