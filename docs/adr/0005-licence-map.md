<!--
SPDX-FileCopyrightText: 2026 Reverblabr contributors
SPDX-License-Identifier: CC-BY-SA-4.0
-->

# ADR-0005: MPL-2.0 licence map, REUSE and DCO

- **Status:** Accepted
- **Date:** 2026-09-19

## Context

The licence has to satisfy all of these:

- **Open source**, approved by the OSI.
- **Combinable with proprietary components** in the same program: codecs, machine-learning SDKs, commercial DSP, proprietary plugins, commercial Qt. Companies must also be able to embed the engine in their own products.
- **Modifications to our own code stay open.**
- **GPL-compatible**, so distribution packages that link GPL-built system libraries remain distributable.
- **An explicit patent grant**, since codecs and DSP are patent-heavy.

## Decision

| Content | Licence |
|---|---|
| Source code: engine, GUI, tools, build files | **MPL-2.0** |
| Public interfaces: the D-Bus interface XML, future SDK and plugin-host protocol headers | **MIT** |
| Documentation and artwork (except the logo) | **CC-BY-SA-4.0** |
| Metadata, presets, templates, desktop files, configuration snippets, test audio | **CC0-1.0** |
| Third-party typefaces bundled unmodified (Noto Sans, JetBrains Mono) | **OFL-1.1**, their own |
| The name and logo | Trademark; see `TRADEMARKS.md` |

- **Source files carry the MPL-2.0 Exhibit A notice without the "Incompatible With Secondary Licenses" line.** That keeps the code GPL-compatible.
- **The repository is REUSE-compliant.** Every file carries an SPDX header, or is covered by `REUSE.toml`, and `reuse lint` passes.
- **Contributions come under the Developer Certificate of Origin** (`Signed-off-by` on every commit), inbound = outbound. There is no copyright assignment.
- **Dependencies** are permissive or weak copyleft, and weak-copyleft ones are linked dynamically. **No GPL code runs in our process.** GPL plugins are fine in the separate plugin host, on the user's machine.

## Consequences

- Companies and distributors can combine Reverblabr with proprietary components, while changes to our files stay under the MPL.
- The licence is not on KDE's list of accepted licences, so Reverblabr is an independent project, not a KDE-hosted one. Using KDE Frameworks as libraries is unaffected.
- If joining KDE ever becomes a goal, the fallback is LGPL-2.1-or-later. That has to be decided before the first external contribution, because relicensing later needs every contributor's consent.
- A CI job enforces `reuse lint` and the dependency-licence rules.

## Alternatives considered

- **LGPL-2.1-or-later.** KDE-friendly, but written for libraries. Applied to a whole application it raises relinking questions for every proprietary component, and version 2.1 has no patent grant.
- **Apache-2.0 or MIT.** Equally combinable, but they allow closed, modified forks of our own code.
- **GPL-3.0 or AGPL-3.0.** They forbid distributing the program together with proprietary components, which defeats the goal.
