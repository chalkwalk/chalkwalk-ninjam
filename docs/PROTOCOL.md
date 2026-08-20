# The NINJAM protocol, as this library implements it

Everything you need to write a client, a bot, a server, or a tool that reads a
session. If you only want to know what the functions are, the headers say that;
this says what the *conversation* is.

## The idea, in one paragraph

Latency cannot be beaten, so NINJAM does not try. Everyone plays to a shared
metronome divided into **intervals** — some number of beats at some tempo — and
you hear everybody else's *previous* interval while you play your current one.
The result is a round: you are always answering what was played a moment ago.
This works at any distance, because nothing has to arrive "in time", only
before the interval it belongs to ends.

Two numbers define the grid: **BPM** and **BPI** (beats per interval). At 120
BPM and 16 BPI an interval is eight seconds long.

## Frames

Every message is a 5-byte header followed by a payload.

```
  byte 0      message type
  bytes 1-4   payload length, little-endian uint32
```

```cpp
std::uint8_t header[protocol::kHeaderSize];
protocol::writeFrameHeader(header, type, static_cast<std::uint32_t>(payload.size()));
// write header, then payload
```

Reading is the same in reverse. `readFrameHeader` needs exactly five bytes and
tells you how many more to wait for:

```cpp
protocol::FrameHeader h;
if (protocol::readFrameHeader(fiveBytes, h)) {
    // h.type, h.length -- now read h.length bytes of payload
}
```

**You own the socket.** This library never reads from one. A typical loop
accumulates bytes into a buffer, takes a header off the front when five bytes
are available, waits for `h.length` more, and then dispatches on `h.type`.

## The conversation

```
  client                                    server
    |                                          |
    |<---------------- 0x00 auth challenge ----|   8 random bytes + server capabilities
    |                                          |
    |----- 0x80 auth user --------------------->|   SHA-1 based hash, username, channel count
    |                                          |
    |<---------------- 0x01 auth reply --------|   granted or not, and why not
    |<---------------- 0x02 server config -----|   BPM and BPI: the grid
    |                                          |
    |<---------------- 0x03 user info ---------|   who is here, what channels they have
    |----- 0x82 channel info ------------------>|   what you are sending
    |                                          |
    |         ... then, every interval ...      |
    |                                          |
    |<---------------- 0x3C interval begin ----|   a GUID, a channel, a codec fourcc
    |<---------------- 0x3D interval write ----|   audio, in chunks, until one is final
    |----- 0xBC interval begin ---------------->|   yours
    |----- 0xBD interval write ---------------->|
    |                                          |
    |<--------------> 0xC0 chat <------------->|   both directions, any time
```

Server-to-client types have the high bit clear; client-to-server set. The
library's `Msg` enum names them all.

### Logging in

The hash is where a client most often goes wrong, so it is a function rather
than a description:

```cpp
protocol::AuthChallenge challenge;
protocol::parseAuthChallenge(payload, challenge);

std::uint8_t hash[20];
protocol::computeAuthHash("myname", "mypassword", challenge.challenge, hash);

auto reply = protocol::buildAuthUser(hash, "myname", channelCount);
```

An anonymous login is the username prefixed with `anonymous:` and an empty
password — a server convention rather than a protocol rule, and one most public
servers accept.

`parseAuthReply` gives you `granted` and, when it is false, an `errorMessage`
worth showing the user rather than swallowing.

### The grid

```cpp
protocol::ServerConfig cfg;
protocol::parseServerConfig(payload, cfg);   // cfg.bpm, cfg.bpi
```

Feed those to `IntervalClock`, which turns them into sample counts and tells you
when an interval boundary falls inside the block you are about to render:

```cpp
IntervalClock clock;
clock.configure(cfg.bpm, cfg.bpi, sampleRate);
```

This is the only place the tempo lives. A server can change it mid-session, and
a client that caches BPM somewhere else will drift.

### Audio

Each interval of each channel is one Ogg Vorbis stream, identified by a GUID
that the sender invents:

```
  interval begin  guid, estimated size, fourcc "OGGv", channel index
  interval write  guid, flags, audio bytes        (repeatedly)
  interval write  guid, flags with final set, last bytes
```

Decode with `VorbisDecoder`, feeding it bytes as they arrive; it produces
interleaved float PCM. `VorbisEncoder` goes the other way. Both are ordinary
objects with no threading of their own.

**The GUID is the identity, not the channel.** Two intervals of the same channel
overlap in flight — you are receiving interval N while interval N+1 begins — so
a client that keys decoders by channel will cross the streams. Key by GUID.

### Chat

```cpp
protocol::Chat chat;
protocol::parseChat(payload, chat);   // chat.command, chat.p1 .. chat.p4
```

The command is a short string and the parameters mean different things for
each:

| command | p1 | p2 |
|---|---|---|
| `MSG` | who said it | what they said |
| `PRIVMSG` | who said it | what they said |
| `TOPIC` | who set it | the topic |
| `JOIN` | who joined | |
| `PART` | who left | |

Sending a public message is `buildChat("MSG", "", text)` — the server fills in
who you are. **A bot lives here.** Everything a bot needs to hear arrives as
`MSG`, and everything it says goes back the same way.

## Writing a bot

The smallest useful bot is: connect, authenticate, answer `MSG`, and send
silence every interval so the server does not consider you gone.

The pieces this library gives you are the protocol ones. What you supply:

1. **A socket.** Any TCP client will do.
2. **A clock.** `IntervalClock` says where the boundaries are; something has to
   call it.
3. **Audio, if you make any.** `VorbisEncoder` takes float PCM. A bot that only
   talks can send an empty interval, or none at all.
4. **A thread policy.** If audio is generated on a real-time thread and
   messages are handled elsewhere, `SpscRing` is a lock-free queue for passing
   between exactly two threads — which is the case that has a cheap answer.

## Robustness, and what the parsers guarantee

Every `parse*` function returns `bool` and **never reads past the end of the
payload**, whatever the payload contains. A truncated, malformed or hostile
message makes the parser return false; it does not throw, does not allocate
unboundedly, and does not walk off the buffer.

That is enforced rather than asserted: the test suite sweeps every parser
against every truncation of a valid message — all prefixes, one byte at a time
— and fuzzes them against random payloads. It is the reason `Reader` exists as
a type instead of the parsers indexing the buffer directly.

**A length field is not a promise.** `interval begin` carries an *estimated*
size, and a sender that gets it wrong is not violating anything. Size your
buffers from what arrives.

## What this library does not do

No sockets, no files, no threads, no audio device, no session recording, no
reconnection policy. Those are all real parts of a client and all of them are
yours — the boundary is deliberate, and it is why this can be linked into
anything.

If you want to see one arrangement of those choices,
[antiphon](https://github.com/chalkwalk/antiphon) is a JUCE plugin that uses
this library to put a band of bots in a NINJAM room.
