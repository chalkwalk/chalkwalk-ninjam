// SPDX-License-Identifier: MIT
#include <chalkwalk/ninjam/SessionArchive.h>

#include <catch2/catch_test_macros.hpp>

using namespace chalkwalk::ninjam::archive;

namespace {
const char *kGuidA = "27431cab75089563e832387bcb39c2ab";
const char *kGuidB = "a6ecbbecfec59ed62f992f91c9e056c9";
}  // namespace

TEST_CASE("a user line belongs to the interval above it", "[archive]") {
  std::string text = std::string("interval 1 100 16\n") + "user " + kGuidA +
                     " \"jgorbe@154.47.16.x\" 0 \"default channel\"\n" +
                     "interval 2 120 8\n" + "user " + kGuidB +
                     " \"Sid_Fang\" 1 \"keys\"\n";
  Log log = parseClipsortLog(text);
  REQUIRE(log.clips.size() == 2);
  REQUIRE(log.skippedLines == 0);
  CHECK(log.clips[0].interval == 1);
  CHECK(log.clips[0].bpm == 100);
  CHECK(log.clips[0].bpi == 16);
  CHECK(log.clips[0].username == "jgorbe@154.47.16.x");
  CHECK(log.clips[0].channelName == "default channel");
  CHECK(log.clips[1].interval == 2);
  CHECK(log.clips[1].bpm == 120);
  CHECK(log.clips[1].bpi == 8);
  CHECK(log.clips[1].channel == 1);
}

TEST_CASE("clips before the first interval line are interval 0, not dropped",
          "[archive]") {
  std::string text = std::string("user ") + kGuidA +
                     " \"jgorbe\" 0 \"default channel\"\n" +
                     "interval 1 90 32\n" + "user " + kGuidB +
                     " \"jgorbe\" 0 \"default channel\"\n";
  Log log = parseClipsortLog(text);
  REQUIRE(log.clips.size() == 2);
  CHECK(log.clips[0].interval == 0);
  // It takes the only tempo there is to take.
  CHECK(log.clips[0].bpm == 90);
  CHECK(log.clips[0].bpi == 32);
  CHECK(log.clips[1].interval == 1);
}

TEST_CASE("a bad line is skipped and counted, never fatal", "[archive]") {
  std::string text = std::string("interval 1 100 16\n") +
                     "user nothex \"x\" 0 \"y\"\n" +      // bad guid
                     "user " + kGuidA + " \"x 0 \"y\"\n" + // unterminated
                     "interval x 100 16\n" +              // bad number
                     "\n" +                               // blank: not counted
                     "user " + kGuidB + " \"ok\" 0 \"c\"\r\n";  // CRLF
  Log log = parseClipsortLog(text);
  REQUIRE(log.clips.size() == 1);
  CHECK(log.clips[0].username == "ok");
  CHECK(log.clips[0].channelName == "c");
  CHECK(log.skippedLines == 3);
}

TEST_CASE("a clip is filed under its first hex digit", "[archive]") {
  CHECK(clipPath(kGuidB) == std::string("a/") + kGuidB + ".OGG");
  CHECK(clipPath(kGuidB, "ogg") == std::string("a/") + kGuidB + ".ogg");
  CHECK(clipPath("").empty());
}

TEST_CASE("an uppercase guid is read as the same clip", "[archive]") {
  std::string text = "interval 1 100 16\nuser 27431CAB75089563E832387BCB39C2AB "
                     "\"a\" 0 \"b\"\n";
  Log log = parseClipsortLog(text);
  REQUIRE(log.clips.size() == 1);
  CHECK(log.clips[0].guid == kGuidA);
}
