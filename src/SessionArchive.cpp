// SPDX-License-Identifier: MIT
#include <chalkwalk/ninjam/SessionArchive.h>

#include <cctype>
#include <cstdlib>
#include <sstream>

namespace chalkwalk::ninjam::archive {

namespace {

// A tiny cursor over one line: integers, bare words and "quoted strings".
// The server writes the quoted fields with no escaping, so a quote ends one.
struct Cursor {
  const std::string &s;
  size_t i = 0;

  void space() {
    while (i < s.size() && std::isspace((unsigned char)s[i]))
      ++i;
  }
  bool word(std::string &out) {
    space();
    size_t b = i;
    while (i < s.size() && !std::isspace((unsigned char)s[i]))
      ++i;
    out = s.substr(b, i - b);
    return !out.empty();
  }
  bool integer(int &out) {
    std::string w;
    if (!word(w))
      return false;
    char *end = nullptr;
    long v = std::strtol(w.c_str(), &end, 10);
    if (*end != '\0')
      return false;
    out = (int)v;
    return true;
  }
  bool quoted(std::string &out) {
    space();
    if (i >= s.size() || s[i] != '"')
      return false;
    size_t close = s.find('"', i + 1);
    if (close == std::string::npos)
      return false;
    out = s.substr(i + 1, close - i - 1);
    i = close + 1;
    return true;
  }
};

bool isGuid(const std::string &g) {
  if (g.size() != 32)
    return false;
  for (char c : g)
    if (!std::isxdigit((unsigned char)c))
      return false;
  return true;
}

std::string lower(std::string s) {
  for (char &c : s)
    c = (char)std::tolower((unsigned char)c);
  return s;
}

}  // namespace

Log parseClipsortLog(const std::string &text) {
  Log log;
  int interval = 0, bpm = 0, bpi = 0;
  bool tempoKnown = false;
  size_t firstUntimed = 0;  // clips before the first `interval` line

  std::istringstream in(text);
  std::string line;
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    Cursor c{line};
    std::string kind;
    if (!c.word(kind))
      continue;  // blank

    if (kind == "interval") {
      int n, b, p;
      if (c.integer(n) && c.integer(b) && c.integer(p) && b > 0 && p > 0) {
        interval = n;
        bpm = b;
        bpi = p;
        if (!tempoKnown) {
          // The clips logged before any interval line take the first tempo.
          for (size_t k = firstUntimed; k < log.clips.size(); ++k) {
            log.clips[k].bpm = bpm;
            log.clips[k].bpi = bpi;
          }
          tempoKnown = true;
        }
        continue;
      }
    } else if (kind == "user") {
      Clip clip;
      if (c.word(clip.guid) && isGuid(clip.guid) && c.quoted(clip.username) &&
          c.integer(clip.channel) && c.quoted(clip.channelName)) {
        clip.guid = lower(clip.guid);
        clip.interval = interval;
        clip.bpm = bpm;
        clip.bpi = bpi;
        log.clips.push_back(std::move(clip));
        continue;
      }
    }
    ++log.skippedLines;
  }
  return log;
}

std::string clipPath(const std::string &guid, const std::string &ext) {
  if (guid.empty())
    return {};
  return std::string(1, guid[0]) + "/" + guid + "." + ext;
}

}  // namespace chalkwalk::ninjam::archive
