<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0020: The engine's interface is the public API, versioned in its name and only ever added to

- **Status:** Accepted
- **Date:** 2026-09-28

## Context

Since ADR-0017 the engine runs in `reverblabrd` and serves `dbus/io.github.reverblabr.Engine.xml` on the session bus. Anything on the bus as the user can call it. The command-line tool, the scheduler, Auto Run and anybody's own scripts and bridges are going to, so the interface needs a promise: what may be relied on, what may change, and how a client finds out.

What there is:
- **One object, one interface, 47 methods and 25 signals**, every one mirrored member for member by `rb::core::EngineFacade` and held to it by `engine.Facade.MatchesTheInterfaceDefinition`.
- **The window uses nothing else.** Every engine action a person can take in the window is a facade call, so it is already a call on the bus. What the window does without the engine (its own layout, the appearance, file actions through KIO, the all-users helper through KAuth) is not the engine's to offer.
- **Asynchronous throughout.** No method returns a value. A call is answered by a signal, and failures arrive as `Error(code, argsJson)` with a code from `engine/core/errorcodes.h`.
- **Structured data travels as JSON strings**, so a session, a catalogue or an inventory can grow fields without a new D-Bus signature.
- **Nothing says which version of the interface a client is talking to**, and nothing stops a change to a signature from reaching a release unnoticed: the conformance test holds the facade to the XML, not the XML to what was released.

## Decision

**The public API is the engine interface, and its name carries its version: `io.github.reverblabr.Engine1`.** It is served by `reverblabrd` on the object `/io/github/reverblabr/Engine` under the well-known name `io.github.reverblabr.Engine`. The bus name and the path stay without a version: there is one engine, and the name is what activation starts it by. The interface is what changes, so the interface is what is numbered. The XML is `dbus/io.github.reverblabr.Engine1.xml`, MIT, and remains the source of truth.

**Within `Engine1`, things are only ever added:**
- **Allowed:** a new method, a new signal, a new field in a JSON payload, a new error code, a new value of a string that is documented as an open set (a state, an edit's `op`, a block type).
- **Never:** removing or renaming a method or signal, changing an argument's name, type, order or direction, removing a JSON field or changing what it means, removing or renaming an error code, or changing what a call does in a way an existing client would notice.
- **Clients must ignore what they do not know:** unknown JSON fields, unknown signals, unknown error codes (treat as a failure with that code), unknown values of an open set.
- **A change that cannot be made by adding** goes into `io.github.reverblabr.Engine2`, served on the same object beside `Engine1`. `Engine1` stays served for at least one release after that, with its members marked `org.freedesktop.DBus.Deprecated`.

**The released surface is pinned.** `dbus/io.github.reverblabr.Engine1.api` lists every method and signal with its arguments' names and types, and every error code with its argument names. The test `dbus.public-surface` fails when the XML or `errorcodes.h` no longer carries a pinned line exactly, which is a breaking change. It also fails when either carries something the pin does not, so an addition is a line added to the pin on purpose. And it fails when `docs/dbus-api.md` does not name a pinned member or error code, so nothing is public without being documented.

**Errors:**
- **The engine's own failures are the `Error` signal**, with the codes of `errorcodes.h`. The codes are part of the API and pinned, and their arguments are named in `argsJson`. A code the window reports about itself and no engine ever sends (`engine.daemonUnreachable`) is not part of the API. The pin test lists it by name, so a new one is pinned or listed on purpose.
- **The only D-Bus errors the engine interface returns are about the call itself:** `org.freedesktop.DBus.Error.UnknownMethod` for a member it does not have, and `org.freedesktop.DBus.Error.InvalidArgs` for arguments that are not the member's published signature exactly. The generic bridge used to turn an integer into the string "42" and act on it; a public API holds callers to what it publishes. A call that is well formed is always accepted, and its failure, if any, is an `Error` signal. That keeps one way of hearing about what went wrong in the engine, whatever the client.

**One object, keyed by ids, as now**, rather than an object per session, block and recording:
- The facade is already keyed by session and block ids, and JSON carries the structure.
- Object paths cannot hold the ids as they are (a UUID's dashes must be escaped).
- Objects would have to appear and disappear in step with every snapshot and patch: a second bookkeeping of the same state, with a second chance to disagree with it.
- A client that wants the structure reads the snapshot, as the window does.

**The telemetry interface is not public.** `io.github.reverblabr.Telemetry` hands the window a descriptor for memory whose layout is fixed by the build (ADR-0018). It carries a fingerprint so that a window from another build is refused. It may change in any release, and the XML and the docs say so.

**Documentation:**
- `docs/dbus-api.md` describes the API: the names, the asynchronous shape, how to call it and watch it with `busctl`, `gdbus` and `qdbus`, every method and signal, the error codes, and the compatibility rules above.
- It carries the checklist of the window's actions against the calls that do them. An action that is not the engine's is listed as such, and one whose feature does not exist yet (the A/B switch, timers) is listed as arriving with that feature.

**Trust stays D-Bus's own** (ADR-0017): a session bus, one user. No second permission layer.

## Consequences

- A client can rely on `Engine1` for as long as it is served, and tell from introspection whether a member it needs is there.
- Every addition costs one line in the pin and a paragraph in the docs. That is the price of nothing becoming public by accident, and the test says which line is missing.
- The window and `RemoteEngine` speak `Engine1` like any other client. A window from one release and a daemon from the next work together as long as the daemon's `Engine1` has only grown.
- The asynchronous shape is kept, so a script calls and then watches for the answer. `reverblabr-ctl` hides that for everyday use.
- Renaming the interface from `io.github.reverblabr.Engine` to `Engine1` is itself a break, made once, before anything outside the tree used it.

## Alternatives considered

- **An unversioned interface name with a `version` property**, as the desktop portals do. It works, but it needs properties the generic bridge does not have, and a client has to read it before it knows what it may call. Introspection of `Engine1` says the same thing without a round trip.
- **An object per session, block and recording, with `org.freedesktop.DBus.ObjectManager`.** It is friendlier to generic D-Bus browsers, but it keeps a second copy of the session model in the object tree, and every snapshot and patch has to keep the two in step. It can come later as `Engine2` if clients show a need.
- **Synchronous getters beside the asynchronous calls**, for scripts. That means two ways to ask for everything. A synchronous call on the daemon's thread would also block behind a running session's work, which is what ADR-0003's shape avoids. The CLI gives scripts the synchronous feel instead.
- **No pinned surface, only the conformance test.** The conformance test keeps the facade and the XML together. It says nothing when both change together, which is exactly how a public signature would break.
