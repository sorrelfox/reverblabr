<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0026: Broadcast: our own source client, FFmpeg's encoders, and the password in the desktop's secret store

- **Status:** Accepted
- **Date:** 2026-10-29

## Context

The Broadcast block sends a session to an internet radio server: Icecast 2, which most stations and every hosting service run, and SHOUTcast, whose source protocol Icecast speaks too. Listeners join a stream at any moment, so what goes out has to be something a player can start in the middle of: MP3 or AAC frames, or Ogg pages whose headers the server keeps for each new listener. The stream has to come back by itself after a server restart or a dropped network, and the title listeners see has to follow what is playing.

Three things had to be chosen: what speaks to the server, what encodes the audio, and where the password lives.

- **The protocol is small.** An Icecast source sends one HTTP request (`PUT` since Icecast 2.4, `SOURCE` before it) with Basic authentication and the station in `Ice-` headers, and then the audio as the request's body for as long as it streams. A SHOUTcast source sends its password on a line of its own to the port after the listeners', reads `OK2`, and sends `icy-` headers. A new title is a separate `GET /admin/metadata` (Icecast) or `/admin.cgi` (SHOUTcast) request. An Ogg stream carries its title in its own comment header instead, and a new title there is a new logical stream chained onto the last.
- **The encoders are already here.** The Recorder's M4A files are made with the distribution's FFmpeg (libavformat, libavcodec), which on Fedora and Kubuntu has LAME for MP3, libopus, libvorbis and FFmpeg's own AAC encoder, and muxes MP3, ADTS and Ogg to any writer, including memory.
- **The password must not be in the session file**, which people copy and send to each other. The engine runs in `reverblabrd` (ADR-0017), so whatever keeps it has to work in a process with no window. Nothing in the project used a secret store yet; ADR-0004 already names KWallet as part of the native Plasma integration.

Measured against Fedora 44's Icecast 2.4.4 in a container (tests/broadcast): `PUT` with no `Expect` header is answered `200 OK` and the audio follows; MP3, AAC and Vorbis titles set by the requests above show on the server's pages; an Opus stream's title is carried in the stream, which players show, but Icecast 2.4 neither reads Opus comments nor takes a metadata request for an Opus mount.

## Decision

- **Our own source client** (`engine/stream`), over a plain non-blocking socket: Icecast's `PUT`, falling back to `SOURCE` when a server answers `405` or `501`, and SHOUTcast's protocol. Every wait (connecting, sending, the server's answer) can be called off within 50 ms, so a session that stops never waits for a server.
- **FFmpeg encodes**, writing to memory: MP3 through LAME with no tag in front and no index, AAC as ADTS through FFmpeg's own encoder (never libfdk_aac, as for the M4A recordings), Opus and Vorbis through their libraries in Ogg pages half a second long. A format the system's FFmpeg cannot make is listed as unavailable in the catalogue, as the Recorder's are. The stream goes out at the session's rate when that is 44.1 or 48 kHz, at 48 kHz otherwise and always for Opus, converted on the stream's thread with the recordings' resampler.
- **The audio thread only copies.** The block writes each cycle into a ten-second tap (the Recorder's `rt::AudioTap`, ADR-0010), and a thread for each Broadcast block drains it, encodes, sends and reconnects.
- **Reconnecting backs off**: one second, doubling to a minute, shortened at random by up to a fifth; a connection that lasted ten seconds starts the count again. A refused password, no password, no host or a missing encoder stops trying until something is changed, and says so once.
- **The title** is a template (`%artist - %title` by default) filled from what is playing: a text file the block is pointed at, looked at once a second, or a track handed to the stream by anything that knows (`BroadcastStream::setNowPlaying`), which is where a media player's now-playing will come in. MP3 and AAC titles go to the server's metadata request; an Ogg stream's go in a new chained stream's comment header.
- **The password lives in the desktop's secret store, through QtKeychain**, in the engine: KWallet on Plasma, the Secret Service elsewhere, under the folder "Reverblabr" and the key `broadcast/<session>/<block>`. QtKeychain's plain-text fallback is off: with no store, a password is refused, not written down. The window hands a new password to the engine once (`SetBroadcastPassword`, over the session bus, which only the user's own processes reach); the engine reads it from the store when a stream starts, keeps it in memory while the session runs, and removes a session's passwords when the session is deleted.

## Consequences

- One new build dependency, QtKeychain for Qt 6 (`qtkeychain-qt6-devel`, `qtkeychain-qt6-dev`), small and in both distributions, and **optional** as RNNoise is (ADR-0016): a build without it has no Broadcast block (`RB_BUILD_BROADCAST`), since a stream with nowhere safe to keep its password is not one to offer. The package's spec asks for it by name. The protocol and the encoders add nothing.
- The bit rates offered are the ones each encoder takes for the channels chosen (Vorbis from 48 kbit/s for stereo and to 192 for mono, Opus to 256 for mono); a setting outside them is streamed at the nearest one. The encoder is made before the server is asked, so settings it refuses never take a mount point, and are not tried again until they change.
- Nothing the engine waits on can hold a stopping session: the host's name is looked up on a thread that is left behind when the wait is called off, the title request has a thread of its own, and a now-playing file is read only when it is a regular file, without waiting.
- A Broadcast block taken out of a session keeps its password while an undo can bring it back; when the session is closed and its history goes, the passwords of blocks no longer in it are removed.
- The connection is plain TCP: a password goes to the server as Icecast's protocol sends it, in the clear unless the network is trusted. TLS is not offered yet; a server that needs it is out of reach until it is.
- An Opus station on Icecast 2.4 shows no title on the server's own pages; listeners' players show it.
- Everything below the engine runs without Qt, PipeWire or a server: the unit tests stream to a server the test makes, and an integration test streams every format to a real Icecast in a container (`ctest --preset container`).
- A copied or imported session has no password for its Broadcast blocks until one is set again, which is the point.

## Alternatives considered

- **libshout.** It speaks the same protocols and adds TLS, but takes no part in encoding: the audio would still be FFmpeg's. It would be one more library for both distributions to package and the build to require, its blocking calls would need its non-blocking mode and our own polling anyway to stop promptly, and the title of an Ogg stream would still have to be put in the stream by us. Our client is a few hundred lines with tests against both a made-up server and a real one.
- **FFmpeg's own `icecast://` protocol.** It sends the stream, but cannot set a title on a running stream, does not speak SHOUTcast's protocol, and reports a lost connection only as a write error with no reason, which the status and the backoff need.
- **libsecret or KWallet directly.** QtKeychain is the same thing behind one API, picks the store the desktop has, and is what other KDE applications use for the same job.
- **The password in the session, scrambled.** Anybody with the file has the password; a session is a file people share.
