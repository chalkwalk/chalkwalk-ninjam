// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>

// The server's session archive: which Ogg clip belongs to whom, on which
// channel, in which interval.
//
// NOT A FORMAT OF OURS. `ninjamsrv` writes it for every session it archives:
// one Ogg Vorbis file per user, per channel, per interval, under a directory
// per session, plus a `clipsort.log` naming them:
//
//   interval <n> <bpm> <bpi>
//   user <32-hex-guid> "<username>" <channel> "<channel name>"
//
// Text in, structures out. Like the rest of this library there is no file
// here: the caller reads the log and opens the clips, so this works wherever
// the bytes come from.
//
// THE READING, and why each part is so:
//
//  - A `user` line belongs to the most recent `interval` line above it. The
//    server logs `user` when an upload BEGINS and `interval` when its loop
//    counter advances, so carrying the interval forward is right.
//  - A log can open with `user` lines before any `interval` line: the session
//    directory is created on a periodic check that can land mid-interval, and
//    the upload already in flight is logged first. Those clips are interval 0
//    and are real audio; dropping them loses the front of every session. They
//    take the tempo of the first `interval` line, the only one there is.
//  - Tempo can change mid-session, so every clip carries the bpm and bpi in
//    force when it was sent.
//  - The server rotates to a new directory every half hour and numbers
//    intervals from 1 again in each, so a jam is several logs in order.
//  - A line that does not parse is skipped and counted, never fatal: an
//    archive is a record of something that happened, and a partial reading of
//    it is worth more than none.

namespace chalkwalk::ninjam::archive {

struct Clip {
  int interval = 0;
  int bpm = 0;
  int bpi = 0;
  std::string guid;       // 32 lowercase hex digits
  std::string username;   // as the server logged it, `name@ip` for anonymous
  int channel = 0;
  std::string channelName;
};

struct Log {
  std::vector<Clip> clips;  // in log order
  int skippedLines = 0;     // lines that were neither form, blank excepted
};

Log parseClipsortLog(const std::string &text);

// Where a clip's file is, relative to the session directory: the server files
// each under a subdirectory named for the GUID's first hex digit, as
// `<d>/<guid>.<ext>`. The server writes the extension `OGG`; pass another if a
// copy has been renamed.
std::string clipPath(const std::string &guid, const std::string &ext = "OGG");

}  // namespace chalkwalk::ninjam::archive
