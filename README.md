# chalkwalk-ninjam

The NINJAM wire protocol, in C++20. JUCE-free, MIT.

[NINJAM](https://www.cockos.com/ninjam/) is a way of playing music with people
over the internet without pretending latency does not exist: everyone plays to
a metronome, and you hear the *previous* interval of what everyone else played.
It works, and it is the only approach that does at intercontinental distances.

| | |
|---|---|
| `NinjamProtocol.h` | The wire format: frames, auth, server config, user info, intervals, chat |
| `VorbisCodec.h` | Ogg Vorbis encode and decode, which is what NINJAM carries |
| `Sha1.h` | The hash the login handshake is built on |
| `IntervalClock.h` | Interval boundaries from a BPM and a beats-per-interval |
| `SpscRing.h` | Lock-free single-producer/single-consumer queue for the audio thread |
| `ChannelMix.h` | Summing remote channels with per-channel gain and pan |
| `Bytes.h` | A byte buffer and little-endian conversion |
| `RoomConventions.h` | Not the protocol: the `[key: ...]` and `/key` forms, and what a `!vote` will take |

## Documentation

**[docs/PROTOCOL.md](docs/PROTOCOL.md)** is the guide: what the conversation
is, frame by frame, with the login handshake, the interval grid, how audio is
carried, and what it takes to write a client or a bot. Start there — the
headers tell you what the functions are, and that tells you what they are for.

## What this is not

**One header is not the protocol, and says so.** `RoomConventions.h` carries
the habits clients have grown around NINJAM's silences -- a room has a key, the
protocol has no field for one, so it rides in chat where every client at least
shows it as text. It is here because more than one client of ours needs it and
none is beneath the others, and because every choice in it is a NINJAM fact
even though NINJAM defines none of them. Text in, text out: it is the envelope,
and what is inside is somebody else's business. Nobody is obliged to speak any
of it; it is here so that clients which choose to can interoperate out of the
box.

**It is not a client.** There is no socket, no file, no thread and no audio
device here — this parses and builds messages, and everything else is yours.
That boundary is deliberate: the client in the plugin this came from carries
`juce::File`, `juce::AudioBuffer` and a lock, and those are host concerns that
a protocol library has no business owning.

So this is the half that is reusable, and it is the half that does not already
exist: a NINJAM implementation you can link into anything, with no framework
attached.

## Provenance, and why this is MIT

The original NINJAM sources are GPLv2. **They were read and never used.** No
NINJAM code was copied, vendored, or has ever been in this repository's
history — this is an independent implementation written from observed protocol
behaviour, in the way interoperating implementations have always been written.

Protocol behaviour is not copyrightable and interop implementations are well
established, so a permissive licence is defensible. But relicensing is a
decision rather than a default, so it is stated here rather than assumed: this
is MIT, deliberately, and the reasoning is above so that anyone adopting it can
judge it for themselves.

## Build and test

```sh
cmake -B build && cmake --build build && ctest --test-dir build
```

Clone with `--recursive`: libogg and libvorbis are vendored as submodules
rather than found on the system, because a system package is not something a
library can rely on -- the first CI run proved it by passing on Linux and
failing on macOS and Windows. Standalone with nothing else, which is the test
of the boundary rather than a convenience.

The tests came from a JUCE plugin and use `juce::UnitTest`'s verbs, which a
JUCE-free library cannot link. They are shimmed onto Catch2 rather than
restructured — restructuring is where a port quietly drops an assertion, and
for a wire protocol that is the worst thing to drop silently. The assertion
count is comparable across the move, and both parsers were sabotage-tested
afterwards to prove the ported tests still bite.

## Licence

MIT. See [LICENSE](LICENSE).

Part of the [chalkwalk](https://github.com/chalkwalk) plugin ecosystem,
alongside [chalkwalk-music](https://github.com/chalkwalk/chalkwalk-music),
[chalkwalk-dsp](https://github.com/chalkwalk/chalkwalk-dsp),
[chalkwalk-tape](https://github.com/chalkwalk/chalkwalk-tape) and
[chalkwalk-physical](https://github.com/chalkwalk/chalkwalk-physical).
