// SPDX-License-Identifier: MIT
#pragma once

#include <string>

// Conventions carried in ordinary chat, which NINJAM itself knows nothing about.
//
// NOT THE PROTOCOL. Everything else in this library is on the wire and defined
// by what a server sends; none of this is. These are the habits clients have
// grown around NINJAM's silences -- a room has a key and a chord chart, the
// protocol carries neither, so they ride in `MSG` where every client at least
// shows them as text.
//
// They are here rather than in one client because two of ours need them and
// neither is beneath the other, and because they are NINJAM-SHAPED even though
// NINJAM does not define them. Every choice below is a fact about how NINJAM
// servers and clients behave:
//
//   - the bracketed form is inert: a client that does not know it shows it
//     verbatim rather than eating it;
//   - it must survive in the room TOPIC, because the topic is the only piece of
//     room state a late joiner inherits -- the server sends it to the joining
//     client and replays no chat at all;
//   - the slash form is line-leading only, because a form matched anywhere is
//     unsayable: any sentence explaining it performs it, so nothing could tell
//     a player how to change the key without changing it for them;
//   - a slash command a client does not recognise is passed through as ordinary
//     chat, which is what makes the second form work at all. Verified against
//     JamTaba.
//
// TEXT IN, TEXT OUT. Nothing here parses a key or a chord: this is the
// envelope, and what is inside it is music. That is what keeps this library
// free of a music-theory dependency, and it is the right seam anyway -- a
// client that carries some other notation can reuse the envelope unchanged.
//
// Nobody is obliged to speak any of this. It is here so that clients which
// choose to can interoperate out of the box.

namespace chalkwalk::ninjam::conventions {

namespace detail {

inline char lower(char c) {
  return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

inline std::string lowered(std::string s) {
  for (auto &c : s)
    c = lower(c);
  return s;
}

inline std::string trim(const std::string &s) {
  const auto ws = [](char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
  };
  size_t b = 0, e = s.size();
  while (b < e && ws(s[b]))
    ++b;
  while (e > b && ws(s[e - 1]))
    --e;
  return s.substr(b, e - b);
}

} // namespace detail

// ---------------------------------------------------------------------------
// The key.

inline std::string keyTagPrefix() { return "[key:"; }

// `[key: D minor]`. The form that may appear ANYWHERE in a line, including in
// the room topic. Empty in, empty out.
inline std::string buildKeyTag(const std::string &keyName) {
  if (keyName.empty())
    return {};
  return "[key: " + keyName + "]";
}

// The sayable form: `/key D minor`. What to tell a player to type.
//
// It exists precisely because the tag above is unsayable -- a bot quoting the
// tag would set the key by explaining it.
inline std::string keyAdviceLine(const std::string &keyName) {
  return "/key " + keyName;
}

// The text inside a `[key: ...]` tag found anywhere in the line, or empty.
// What is returned is whatever was written; whether it names a key is the
// caller's question.
inline std::string extractKeyTag(const std::string &line) {
  const auto prefix = keyTagPrefix();
  const auto at = detail::lowered(line).find(detail::lowered(prefix));
  if (at == std::string::npos)
    return {};

  const size_t from = at + prefix.size();
  const auto close = line.find(']', from);
  if (close == std::string::npos)
    return {};
  // Trimmed: the space in `[key: D minor]` is the convention's, not the
  // caller's. Handing it back would make every consumer trim it, and one of
  // them would forget.
  return detail::trim(line.substr(from, close - from));
}

// Either form: the tag anywhere, or a LINE-LEADING `/key`. Empty when the line
// is not a key announcement at all.
inline std::string extractKeyAnnouncement(const std::string &line) {
  if (auto tagged = extractKeyTag(line); !tagged.empty())
    return tagged;

  const auto trimmed = detail::trim(line);
  const std::string slash = "/key ";
  if (detail::lowered(trimmed).rfind(detail::lowered(slash), 0) != 0)
    return {};
  return trimmed.substr(slash.size());
}

// ---------------------------------------------------------------------------
// What the server will accept in a `!vote`.
//
// These ARE protocol-adjacent facts rather than a convention of ours: they are
// the stock server's limits, and a value outside them is answered with a
// complaint about the command's parameters, which tells a player nothing. A
// client does better to refuse first and say why.
//
// The stock build's figures. A server may be built with different ones -- a
// public room has been seen at 124 BPI -- so treat these as what to expect
// rather than as a guarantee.

inline bool isVotableBpm(int bpm) { return bpm >= 40 && bpm <= 400; }
inline bool isVotableBpi(int bpi) { return bpi >= 2 && bpi <= 64; }

// An admin setting the room directly is held to a wider range than a vote.
inline bool isAdminSettableBpm(int bpm) { return bpm >= 20 && bpm <= 400; }
inline bool isAdminSettableBpi(int bpi) { return bpi >= 2 && bpi <= 1024; }

} // namespace chalkwalk::ninjam::conventions
